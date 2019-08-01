using Task = Microsoft.Build.Utilities.Task;
using TaskItem = Microsoft.Build.Utilities.TaskItem;

using MessageImportance = Microsoft.Build.Framework.MessageImportance;
using ITaskItem = Microsoft.Build.Framework.ITaskItem;
using Required = Microsoft.Build.Framework.RequiredAttribute;

using ProjectCollection = Microsoft.Build.Evaluation.ProjectCollection;
using Project = Microsoft.Build.Evaluation.Project;
using ProjectProperty = Microsoft.Build.Evaluation.ProjectProperty;
using ProjectItem = Microsoft.Build.Evaluation.ProjectItem;

using CLCommandLine = Microsoft.Build.CPPTasks.CLCommandLine;
using GetOutOfDateItems = Microsoft.Build.CPPTasks.GetOutOfDateItems;
using MSBuild = Microsoft.Build.Tasks.MSBuild;
using Exec = Microsoft.Build.Tasks.Exec;

using String = System.String;
using Convert = System.Convert;
using Type = System.Type;
using System.Collections.Generic;
using PropertyInfo = System.Reflection.PropertyInfo;
using FileInfo = System.IO.FileInfo;
using File = System.IO.File;
using Path = System.IO.Path;
using Directory = System.IO.Directory;
using Stopwatch = System.Diagnostics.Stopwatch;

using System.Linq;

public class CppM_CL : Task
{
	[Required] public ITaskItem[] CL_Input_All { get; set; }
	[Required] public ITaskItem[] CL_Input_Manually_Selected { get; set; }
	[Required] public ITaskItem[] CurrentProjectHolder { get; set; }
	[Required] public ITaskItem[] ProjectReference { get; set; }
	[Required] public String SolutionFilePath { get; set; }
	[Required] public String Configuration { get; set; }
	[Required] public String Platform { get; set; }
	[Required] public String PlatformToolset { get; set; }
	[Required] public String PreProcess_Only { get; set; }
	[Required] public String ClangScanDepsPath { get; set; }
	[Required] public String IntDir { get; set; }
	[Required] public String NinjaExecutablePath { get; set; }
	
	// note: remember CppM_BeforeClean when changing this
	string PP_ModuleMapFile			= "pp.modulemap";
	string PP_CompilationDatabase 	= "pp_commands.json";
	string ModuleMapFile 			= "module_map.json";
	string NinjaBuildFile 			= "build.ninja";
	string NinjaRecursiveBuildFile 	= "build_rec.ninja";
	
	// todo: replace this with a compiler enum
	bool IsLLVM() {
		var toolset = PlatformToolset.ToLower();
		return toolset == "llvm" || toolset == "clangcl";
	}
	
	void CreateProjectCollection(string measure_message) {
		CallMeasured<bool>(measure_message, () => CreateProjectCollection());
	}
	
	// load all transitively referenced projects
	// todo: keep this in memory somehow when building a project hierarchy
	// maybe even run a daemon ?
	bool CreateProjectCollection() {
		string CurrentProject = CurrentProjectHolder[0].GetMetadata("FullPath");
		
		ProjectCollection project_collection = new ProjectCollection( new Dictionary<String, String> {
			{ "SolutionDir", Path.GetDirectoryName(SolutionFilePath) + "\\" },
			{ "Configuration", Configuration }, { "Platform", Platform },
			{ "SolutionPath", SolutionFilePath } // just for completeness
		});
		//project_collection = ProjectCollection.GlobalProjectCollection; // todo: check if this is faster ?
		
		var project_queue = new Queue<string>();
		var loaded_projects = new HashSet<string>();
		project_queue.Enqueue(CurrentProject);
		loaded_projects.Add(CurrentProject);
		
		bool first = true;
		while(project_queue.Count > 0) {
			var project_file = project_queue.Dequeue();
			var project = project_collection.LoadProject(project_file);
			
			// skip projects like ZERO_CHECK and others that aren't using cpp_modules
			var int_dir = project.GetPropertyValue("CppM_IntDir_FullPath");
			if(int_dir == "") {
				project_collection.UnloadProject(project);
				continue;
			}
			
			var target = new Target{ project = project, int_dir = int_dir };
			all_targets.Add(target);
			if(first) {	root_target = target; first = false;	}
			
			foreach(ProjectItem reference in project.GetItems("ProjectReference")) {
				string path = reference.GetMetadataValue("FullPath");
				if(loaded_projects.Add(path))
					project_queue.Enqueue(path);
			}
		}
		return true;
	}
	
	string GetImportableHeaderModuleName(string header_path) {
		return Path.GetFileNameWithoutExtension(header_path).ToUpper();
	}
	
	void WriteAllLines_IfChanged(string file_path, IEnumerable<string> lines) {
		if(!File.Exists(file_path) || !lines.SequenceEqual(File.ReadAllLines(file_path)))
			File.WriteAllLines(file_path, lines);
	}
	
	HashSet<string> GetImportableHeaders()
	{
		// merge the sets of importable headers from all transitively referenced projects
		var all_importable_headers = all_targets.SelectMany(target =>
			//todo: use the CppM_PreProcess target
			target.project.GetItems("ClCompile")
				.Where(i => i.GetMetadataValue("CppM_Header_Unit") == "true"
					&& i.GetMetadataValue("ExcludedFromBuild") != "true")
				.Select(i => NormalizeFilePath(i.GetMetadataValue("FullPath")))
		).ToHashSet();
		
		// create a module map for the preprocessor to know which headers are importable
		/*WriteAllLines_IfChanged(IntDir + PP_ModuleMapFile, all_importable_headers
			.Select(header => "module " + GetImportableHeaderModuleName(header) + 
				" {\n header \"" + PortablePath(header) + "\"\n export *\n}"));*/

		return all_importable_headers;
	}
	
	static string NormalizeFilePath(string file_path) {
		try { return Path.GetFullPath(file_path).ToUpper(); }
		catch (System.Exception e) {
			throw new System.Exception(String.Format("failed to normalize path {0}", file_path), e);
		}
	}
	
	public class TLogSet
	{
		public string src_file = "";
		public List<string> command = new List<string>(); // just for consistency
		public List<string> inputs = new List<string>();
		public List<string> outputs = new List<string>();
		
		public List<string> GetLines(int i) {
			if(i == 0) return outputs;
			else if(i == 1) return inputs;
			else return command;
		}
		public IEnumerable<string> GetOutputLines(int i) {
			return GetLines(i).Prepend("^" + src_file);
		}
		public string GetCommand() {
			return command.First();
		}
		public void SetCommand(string cmd) {
			command.Clear(); command.Add(cmd);
		}
	}
	
	string[] rws = new string[]{"write", "read", "command"};
	
	void ReadTLogs(ItemSet itemset, string directory)
	{
		for(int i = 0; i < 3; i++) {
			Item cur_item = null;
			bool skip = true;
			try {
				foreach(string line in File.ReadAllLines(directory + "cppm." + rws[i] + ".1.tlog")) {
					if(line.StartsWith("^")) {
						skip = true;
						// only read the tlogs if the source file still exists (TLogSet was initialized)
						if(itemset.TryGet(line.Substring(1), out cur_item)) {
							// don't read the old tlogs if newer ones have already been loaded
							skip = cur_item.tlog_set.GetLines(i).Count > 0;
						}
					} else if(!skip) {
						cur_item.tlog_set.GetLines(i).Add(i == 2 ? line : NormalizeFilePath(line));
					}
				}
			} catch(System.IO.FileNotFoundException) {
				// it's ok, if it doesn't exist just leave this part of the TLogSet empty
				//Log.LogMessage(MessageImportance.High, "could not read {0}", directory + "cppm." + rws[i] + ".1.tlog");
			}
		}
	}
	
	void WriteTLogs(ItemSet itemset) {
		foreach(var group in itemset.Items.GroupBy(i => i.tlog_dir)) {
			var directory = group.Key;
			for(int i = 0; i < 3; i++) {
				File.WriteAllLines(
					directory + "cppm." + rws[i] + ".1.tlog",
					group.SelectMany(item => item.tlog_set.GetOutputLines(i)).ToArray()
				);
			}
		}
	}
	
	string GetBaseCommand(Item item, bool preprocess)
	{
		// copy metadata from the source items into CLCommandLine
		var cmdline = new CLCommandLine();
		foreach(string prop in props) {
			// this makes sure the command doesn't change when we call msbuild on other projects for preprocessing:
			if(props_not_for_cmd.Contains(prop)) continue;
			string meta_value = item.GetMetadata(prop);
			if(meta_value != "" && !SetPropertyFromString(cmdline, prop, meta_value)) {
				Log.LogMessage(MessageImportance.High, "failed to set {0}", prop);
				throw new System.Exception("failed to set property for CLCommandLine");
			}
		}
		if(preprocess) {
			cmdline.EnableModules = false; // for clang-scan-deps
			cmdline.AdditionalOptions = ""; // todo: just remove /module:stdIfcDir
		} else {
			if(!IsLLVM()) // serialize writes to the PDB files (otherwise cl.exe fails to open them in parallel)
				cmdline.AdditionalOptions += " /FS ";
		}
		if(!cmdline.Execute())
			throw new System.Exception("failed to execute CLCommandLine");
		string tool = cmdline.ToolExe;
		if(!IsLLVM()) // for some reason CLToolExe no longer seems to bet set for MSVC
			tool = "cl.exe";
		string args = cmdline.CommandLines[0].GetMetadata("Identity");
		//if(IsLLVM() && !preprocess) // todo: at some point scan-deps will also need to know the set of importable headers
		//	args += " -Xclang \"-fmodule-map-file=" + item.target.int_dir + PP_ModuleMapFile + "\" ";
		var name = item.path;
		if(IsLLVM()) name = PortablePath(name);
		return String.Format("\"{0}\" \"{1}\" {2}", tool, name, args);
	}
	
	ITaskItem[] GetOOD(string tlog_directory, IEnumerable<Item> items, bool preprocess)
	{
		var get_ood_sources = new List<TaskItem>();
		foreach(var item in items) {
			item.tlog_set.SetCommand(GetBaseCommand(item, preprocess));
			var ood_item = new TaskItem(item.tlog_set.src_file);
			ood_item.SetMetadata("write", String.Join(";", item.tlog_set.outputs));
			ood_item.SetMetadata("read", String.Join(";", item.tlog_set.inputs));
			ood_item.SetMetadata("command", item.tlog_set.GetCommand());
			//Log.LogMessage(MessageImportance.High, "{0} - {1} - dir {2}", item.GetMetadata("FileName"), item.tlog_set.GetCommand(), tlog_directory);
			get_ood_sources.Add(ood_item);
		}
		
		var get_ood = new GetOutOfDateItems {
			BuildEngine 			  = this.BuildEngine,
			Sources                   = get_ood_sources.ToArray(),
			OutputsMetadataName       = rws[0],
			DependenciesMetadataName  = rws[1],
			CommandMetadataName       = rws[2],
			TLogDirectory             = tlog_directory,
			TLogNamePrefix            = "cppm",
			CheckForInterdependencies = false
		};
		
		if(!get_ood.Execute())
			throw new System.Exception("failed to execute GetOutOfDateItems");
		
		Log.LogMessage(MessageImportance.High, "OOD {0} - {1}", Path.GetFileName(tlog_directory.TrimEnd(Path.DirectorySeparatorChar)), 
			String.Join(",",get_ood.OutOfDateSources.Select(item => item.GetMetadata("FileName"))));
		
		return get_ood.OutOfDateSources;
	}

	ItemSet ReadTLogs_GetOOD() {
		var itemset = new ItemSet();
		foreach(var item in CL_Input_All)
			itemset.Add(item, root_target);
		ReadTLogs_GetOOD(ref itemset); 
		return itemset;
	}

	void ReadTLogs_GetOOD(ref ItemSet itemset) {
		if(itemset.Items.Count == 0)
			return;
		foreach(var item in itemset.Items)
			item.EnsureInitTLogSet();
		foreach(var group in itemset.Items.GroupBy(i => i.tlog_dir)) {
			var tlog_dir = group.Key;
			ReadTLogs(itemset, tlog_dir);
			foreach(var itaskitem in GetOOD(tlog_dir, group, preprocess: true))
				itemset.Get(itaskitem.GetMetadata("FullPath")).is_ood = true;
		}
	}
	
	bool SetPropertyFromString(object obj, string prop, string value)
	{
		//Log.LogMessage(MessageImportance.High, "{0} = {1}", prop, value);
		PropertyInfo propertyInfo = obj.GetType().GetProperty(prop);
		if(propertyInfo == null) {
			Log.LogMessage(MessageImportance.High, "	property " + prop + " not found in object");
			return true;
		}
		object value_to_set = null;
		if(propertyInfo.PropertyType.IsArray) {
			Type t = propertyInfo.PropertyType.GetElementType();
			if(t == typeof(String)) {
				value_to_set = value.ToString().Split(new char[] {';'});
			} else if(t == typeof(ITaskItem)) {
				string[] elems = value.ToString().Split(new char[] {';'});
				value_to_set = elems.Select(e => new TaskItem(e)).ToArray();
			} else {
				Log.LogMessage(MessageImportance.High, "ERROR: unknown array type");
				return false;
			}
		} else {
			value_to_set = Convert.ChangeType(value, propertyInfo.PropertyType);
		}
		propertyInfo.SetValue(obj, value_to_set, null);
		return true;
	}
	
	string GetPropertyFromString(object obj, string prop)
	{
		PropertyInfo propertyInfo = obj.GetType().GetProperty(prop);
		if(propertyInfo == null) {
			Log.LogMessage(MessageImportance.High, "	property " + prop + " not found in object");
			return "";
		}
		object value = propertyInfo.GetValue(obj);
		if(value == null)
			return "";
		if(propertyInfo.PropertyType.IsArray) {
			Type t = propertyInfo.PropertyType.GetElementType();
			if(t == typeof(string)) {
				var array = value as string[];
				return String.Join(";", array);
			} else if(t == typeof(ITaskItem)) {
				var array = value as ITaskItem[];
				return String.Join(";", array.Select(item => item.ToString()));
			} else {
				Log.LogMessage(MessageImportance.High, "ERROR: unknown array type");
				return "";
			}
		}
		return value.ToString();
	}
	
	// replaced ' +(\w+) +="([^"]+)"\r\n' with '"\1", '
	// changed BuildingInIDE to BuildingInIde, commented TrackFileAccess, MinimalRebuild, MinimalRebuildFromTracking and AcceptableNonZeroExitCodes
	string[] props = { "BuildingInIde", "Sources", "AdditionalIncludeDirectories", "AdditionalOptions", "AdditionalUsingDirectories", "AssemblerListingLocation", "AssemblerOutput", "BasicRuntimeChecks", "BrowseInformation", "BrowseInformationFile", "BufferSecurityCheck", "CallingConvention", "ControlFlowGuard", "CompileAsManaged", "CompileAsWinRT", "CompileAs", "ConformanceMode", "DebugInformationFormat", "DiagnosticsFormat", "DisableLanguageExtensions", "DisableSpecificWarnings", "EnableEnhancedInstructionSet", "EnableFiberSafeOptimizations", "EnableModules", "EnableParallelCodeGeneration", "EnablePREfast", "EnforceTypeConversionRules", "ErrorReporting", "ExceptionHandling", "ExcludedInputPaths", "ExpandAttributedSource", "FavorSizeOrSpeed", "FloatingPointExceptions", "FloatingPointModel", "ForceConformanceInForLoopScope", "ForcedIncludeFiles", "ForcedUsingFiles", "FunctionLevelLinking", "GenerateXMLDocumentationFiles", "IgnoreStandardIncludePath", "InlineFunctionExpansion", "IntrinsicFunctions", "LanguageStandard", /*"MinimalRebuild",*/ "MultiProcessorCompilation", "ObjectFileName", "OmitDefaultLibName", "OmitFramePointers", "OpenMPSupport", "Optimization", "PrecompiledHeader", "PrecompiledHeaderFile", "PrecompiledHeaderOutputFile", "PREfastAdditionalOptions", "PREfastAdditionalPlugins", "PREfastLog", "PreprocessKeepComments", "PreprocessorDefinitions", "PreprocessSuppressLineNumbers", "PreprocessToFile", "ProcessorNumber", "ProgramDataBaseFileName", "RemoveUnreferencedCodeData", "RuntimeLibrary", "RuntimeTypeInfo", "SDLCheck", "ShowIncludes", "WarningVersion", "SmallerTypeCheck", "SpectreMitigation", "StringPooling", "StructMemberAlignment", "SupportJustMyCode", "SuppressStartupBanner", "TreatSpecificWarningsAsErrors", "TreatWarningAsError", "TreatWChar_tAsBuiltInType", "UndefineAllPreprocessorDefinitions", "UndefinePreprocessorDefinitions", "UseFullPaths", "UseUnicodeForAssemblerListing", "WarningLevel", "WholeProgramOptimization", "WinRTNoStdLib", "XMLDocumentationFileName", "CreateHotpatchableImage", "TrackerLogDirectory", "TLogReadFiles", "TLogWriteFiles", "ToolExe", "ToolPath", /*"TrackFileAccess", "MinimalRebuildFromTracking",*/ "ToolArchitecture", "TrackerFrameworkPath", "TrackerSdkPath", "TrackedInputFilesToIgnore", "DeleteOutputOnExecute", /*"AcceptableNonZeroExitCodes",*/ "YieldDuringToolExecution" };
	// we should not recompile the files if these properties change:
	string[] props_not_for_cmd = { "BuildingInIde", "ErrorReporting" };
	
	// returns true if a_file is newer than b_file
	// false if b_file exists but a_file does not
	// true if neither file exists
	bool NewerThan(string a_file, string b_file) {
		FileInfo a_info = new FileInfo(a_file);
		FileInfo b_info = new FileInfo(b_file);
		return a_info.LastWriteTime >= b_info.LastWriteTime;
	}
	
	public class ModuleDefinition
	{
		public string cmi_file = "";
		public string obj_file = "";
		public string exported_module = "";
		public bool importable_header = false;
		public List<string> imported_modules = new List<string>();
		public List<string> imported_headers = new List<string>();
		public List<string> included_headers = new List<string>();
		public string base_command = "";
		
		public void assign(ModuleDefinition def) {
			cmi_file = def.cmi_file;
			obj_file = def.obj_file;
			exported_module = def.exported_module;
			importable_header = def.importable_header;
			imported_modules = def.imported_modules;
			imported_headers = def.imported_headers;
			included_headers = def.included_headers;
			base_command = def.base_command;
		}
	}
	
	public class ModuleMap
	{
		public class Entry : ModuleDefinition {
			public string source_file = "";
			public string mdef_file = "";
			
			public static Entry create(string src_file, string mdef_file, ModuleDefinition module_def) {
				Entry ret = new Entry { source_file = src_file, mdef_file = mdef_file };
				ret.assign(module_def);
				return ret;
			}
		}
		public List<Entry> entries = new List<Entry>();
	}
	
	string ToJSON<T>(T obj) {
		return Newtonsoft.Json.JsonConvert.SerializeObject(obj, Newtonsoft.Json.Formatting.Indented);
	}
	
	void SerializeToFile<T>(T obj, String file_path)
	{
		//File.WriteAllText(def_file, serializer.Serialize(module_def));
		Directory.CreateDirectory(Path.GetDirectoryName(file_path));
		File.WriteAllText(file_path, ToJSON(obj));
	}
	
	T Deserialize<T>(String file_path)
	{
		// serializer.Deserialize<ModuleDefinition>(File.ReadAllText(def_file));
		return Newtonsoft.Json.JsonConvert.DeserializeObject<T>(File.ReadAllText(file_path));
	}
	
	T DeserializeIfExists<T>(String file_path)
	{
		try {
			//return serializer.Deserialize<T>(File.ReadAllText(file_path));
			return Deserialize<T>(file_path);
		} catch(System.IO.FileNotFoundException) {
			return default(T);
		} catch(System.IO.DirectoryNotFoundException) {
			return default(T);
		}
	}
	
	Dictionary<string, ModuleMap.Entry> GetModuleMap_SrcToEntry(ModuleMap module_map) {
		if(module_map == null)
			return null;
		var src_to_entry = new Dictionary<string, ModuleMap.Entry>();
		foreach(ModuleMap.Entry entry in module_map.entries) {
			src_to_entry.Add(entry.source_file, entry);
		}
		return src_to_entry;
	}
	
	delegate void VoidFunc();
	
	public class CompilationDatabaseEntry {
		public string directory, file, command;
	}
	
	class ExecuteResult { public int exit_code; public ITaskItem[] console_output; }
	ExecuteResult ExecuteCommand(string command, bool pipe_output = false, string measure_message = null) {
		return CallMeasured<ExecuteResult>(measure_message, () => {
			Log.LogMessage(MessageImportance.High, "executing {0}", command);
			var exec = new Exec {
				Command = command,
				ConsoleToMSBuild = pipe_output,
				BuildEngine = this.BuildEngine,
				EchoOff = pipe_output,
				IgnoreExitCode = true,
				StandardOutputImportance = pipe_output ? "low" : "high"
			};
			if(!exec.Execute())
				throw new System.Exception(String.Format("ERROR: failed to execute {0}", command));
			return new ExecuteResult { exit_code = exec.ExitCode, console_output = exec.ConsoleOutput };
		});
	}
	
	void LogScanReport(string src_file, ModuleDefinition mdef) {
		string report = "";
		if(mdef.exported_module != "" && !mdef.importable_header)
			report = "exp: " + mdef.exported_module + " ";
		string imports = String.Join(" ", mdef.imported_modules.Concat(mdef.imported_headers));
		report += (imports != "" ? "imp: " + imports : "");
		if(report != "") Log.LogMessage(MessageImportance.High, "{0} {1}", src_file, report);
	}
	
	delegate string PropToValueFunc(string prop);
	
	abstract class Item {
		public string path = null;
		public TLogSet tlog_set = null;
		public string tlog_dir = null;
		public bool is_ood = false;
		public bool visited = false;
		public Target target = null; // used by RunScanDeps
		public Node node = null; // used by GetSelectedNinjaTargets
		// note: could just access the target through node, but you don't have nodes until after RunScanDeps
		
		public abstract string GetMetadata(string prop);
		public Item Init(Target target) {
			this.path = NormalizeFilePath(GetMetadata("FullPath"));
			this.target = target;
			return this;
		}
		public void EnsureInitTLogSet() {
			if(tlog_set == null) {
				tlog_set = new TLogSet{ src_file = path };
				tlog_dir = GetMetadata("TrackerLogDirectory") + "pp\\";
			}
		}
	}
	class ITaskItem_Wrapper : Item {
		public ITaskItem item = null;
		public override string GetMetadata(string prop) { return item.GetMetadata(prop).ToString(); }
	}
	class ProjectItem_Wrapper : Item {
		public ProjectItem item = null;
		public override string GetMetadata(string prop) { return item.GetMetadataValue(prop); }
	}
	class ItemSet {
		public Dictionary<string, Item> dict = new Dictionary<string, Item>();
		public Dictionary<string, Item>.ValueCollection Items { get { return dict.Values; } }
		public Item Get(string path) {
			return dict[NormalizeFilePath(path)];
		}
		public bool TryGet(string path, out Item item) {
			return dict.TryGetValue(NormalizeFilePath(path), out item);
		}
		public Item TryGet(string path) {
			Item item = null;
			TryGet(path, out item);
			return item;
		}
		public void Add(Item item) { dict.Add(item.path, item); }
		public void Add(ITaskItem item, Target tgt) { Add(new ITaskItem_Wrapper { item = item }.Init(tgt)); }
		public void Add(ProjectItem item, Target tgt) { Add(new ProjectItem_Wrapper { item = item }.Init(tgt)); }
	}
	
	// ideally the scanner should make it unnecessary to pass this to RunScanDeps
	HashSet<string> all_importable_headers_tmp = null;
	
	bool RunScanDeps(ItemSet itemset)
	{
		// create a preprocessor specific compilation database that only includes out of date files
		// and does not ask the compiler to load any modules
		//Log.LogMessage(MessageImportance.High, "generating preprocessor compilation database");
		var compilation_database = itemset.Items
			.Where(i => i.is_ood)
			.Select(i => new CompilationDatabaseEntry { 
				file = i.path, command = i.tlog_set.GetCommand() // todo: directory ???
			});
		SerializeToFile(compilation_database, IntDir + PP_CompilationDatabase);
		
		var command = String.Format("\"{0}\" --compilation-database=\"{1}\"", 
			ClangScanDepsPath, IntDir + PP_CompilationDatabase);
		//Log.LogMessage(MessageImportance.High, "cmd = {0}", command);
		var exec_res = ExecuteCommand(command, pipe_output: true, measure_message: "clang-scan-deps");
		if(exec_res.exit_code != 0) {
			Log.LogMessage(MessageImportance.High, "ERROR: failed to preprocess the sources");
			return false;
		}
		
		ModuleDefinition cur_mdef = null;
		Item cur_item = null;
		VoidFunc finish_cur_mdef = () => {
			if(cur_mdef == null) return;
			if(cur_mdef.exported_module != "") // could be from a named module or imported header
				cur_mdef.cmi_file = cur_item.GetMetadata("CppM_CMI_File");
			string def_file = cur_item.GetMetadata("CppM_ModuleDefinitionFile");
			cur_item.tlog_set.outputs.Clear();
			cur_item.tlog_set.outputs.Add(NormalizeFilePath(def_file));
			SerializeToFile(cur_mdef, def_file);
			var entry_to_add = ModuleMap.Entry.create(cur_item.path, def_file, cur_mdef);
			cur_item.target.map.entries.Add(entry_to_add);
			cur_item.visited = true;
			LogScanReport(cur_item.path, cur_mdef);
			cur_mdef = null;
		};
		int nr_lines = 0;
		foreach(var line_item in exec_res.console_output) {
			if(nr_lines++ == 0) continue;
			var line = line_item.GetMetadata("Identity");
			//Log.LogMessage(MessageImportance.High, "{0}", line);
			//continue;
			if(line.StartsWith(":::: ")) {
				// new source file
				finish_cur_mdef();
				cur_item = itemset.Get(line.Substring(5));
				cur_item.tlog_set.inputs.Clear();
				cur_item.tlog_set.inputs.Add(ClangScanDepsPath);
				// note: clang-scan-deps doesn't execute a separate compiler tool
				
				cur_mdef = new ModuleDefinition {
					base_command = GetBaseCommand(cur_item, preprocess: false),
					obj_file = NormalizeFilePath(cur_item.GetMetadata("ObjectFileName") + 
						Path.GetFileNameWithoutExtension(cur_item.path) + ".obj"),
					importable_header = (cur_item.GetMetadata("CppM_Header_Unit") == "true"),
				};
				if(cur_mdef.importable_header)
					cur_mdef.exported_module = GetImportableHeaderModuleName(cur_item.path);
			} else if(line.StartsWith(":exp ")) {
				// exports module
				var module_name = line.Substring(5);
				cur_mdef.exported_module = module_name;
			} else if(line.StartsWith(":imp ")) {
				// imports module
				var module_name = line.Substring(5);
				// MSVC uses named modules for the standard library
				if(IsLLVM() || !module_name.StartsWith("std."))
					cur_mdef.imported_modules.Add(module_name);
			} else {
				// other dependencies (headers,modulemap,precompiled header cmis)
				var file = NormalizeFilePath(line);
				if(file == cur_item.path) // for OOD detection
					continue;
				cur_item.tlog_set.inputs.Add(file);
				if(all_importable_headers_tmp.Contains(file)) { // todo: shouldn't need to check, the scanner should tell us
					cur_mdef.imported_headers.Add(file);
					cur_mdef.imported_modules.Add(GetImportableHeaderModuleName(file));
				} else {
					var ext = Path.GetExtension(file);
					if(ext != ".MODULEMAP") // todo: in principle this could include other non-header deps :( 
						cur_mdef.included_headers.Add(file);
				}
			}
		}
		finish_cur_mdef();
		
		// write partial tlogs even if we couldn't preprocess all of the sources
		WriteTLogs(itemset);
		
		var remaining_ood = itemset.Items.Where(i => i.is_ood && !i.visited);
		if(remaining_ood.Count() > 0) {
			//foreach(var line_item in exec_res.console_output) // todo: weird, if this is uncommented, the errors below don't show up
			//	Log.LogMessage(MessageImportance.High, "{0}", line_item.GetMetadata("Identity"));
			foreach(var item in remaining_ood)
				Log.LogMessage(MessageImportance.High, "ERROR: source file {0} was not successfully preprocessed", item.path);
			return false;
		}

		return true;
	}
	
	bool IsProjectDirty(string project_file, string module_map_file) {
		// note: NewerThan returns false if project exist but the module map does not
		if(!NewerThan(module_map_file, project_file))
			return true;
		// todo: check imported property sheets, system environment ... ?
		return false;
	}
	
	bool PreProcess(ItemSet itemset)
	{
		Log.LogMessage(MessageImportance.High, "CppM: PreProcess");
		
		var items_by_project = itemset.Items.GroupBy(i => i.target); // todo: is this efficient ?
		// only_current ?
		foreach(var group in items_by_project) {
			var target = group.Key;
			target.map = new ModuleMap();
		}
		
		int nr_ood = itemset.Items.Where(i => i.is_ood).Count();
		// scan out of date files for dependencies, add them to the module map(s)
		if(nr_ood > 0) {
			if(!RunScanDeps(itemset))
				return false;
		}

		foreach(var group in items_by_project) {
			var target = group.Key;
			var module_map_file = target.int_dir + ModuleMapFile;
			var module_map = target.map;

			// even if all of the files up to date, it's still possible
			// for the module map to be out of date, e.g if a file is removed from the project
			if(nr_ood > 0 || IsProjectDirty(target.project.FullPath, module_map_file))
			{
				var old_module_map = DeserializeIfExists<ModuleMap>(module_map_file);
				var old_src_to_entry = GetModuleMap_SrcToEntry(old_module_map);
				foreach(var item in itemset.Items) {
					var src_file = item.path;
					if(item.is_ood) continue;
					ModuleMap.Entry entry_to_add = null;
					string def_file = item.GetMetadata("CppM_ModuleDefinitionFile");
					// if a source file is ood that means it (or some header it includes)
					// is newer than the module definition file
					// so if it's up to date we could just read the definition file
					// but it should be faster to read the definition from the module map file instead
					if(old_module_map != null && NewerThan(module_map_file, def_file)) {
						if(!old_src_to_entry.TryGetValue(src_file, out entry_to_add)) {
							throw new System.Exception(String.Format("ERROR: module map {0} created after " + 
								"definition file {1}, but does not contain an entry for " + 
								"source file {2} ??", module_map_file, def_file, src_file));
						}
					} else {
						// unless the last preprocess didn't finish successfully
						var module_def = DeserializeIfExists<ModuleDefinition>(def_file);
						entry_to_add = ModuleMap.Entry.create(src_file, def_file, module_def);
					}
					module_map.entries.Add(entry_to_add);
				}
				
				SerializeToFile(module_map, module_map_file);
			} else {
				File.SetLastWriteTimeUtc(module_map_file, System.DateTime.UtcNow);
			}
		}
		
		return true;
	}
	
	//info related to a collection of source files that form a project
	public class Target
	{
		public ModuleMap map = null;
		public Project project = null;
		public string int_dir = null;
		public List<Node> nodes = new List<Node>();
		public string ninja = "";
	}
	
	//a node in the build DAG
	public class Node
	{
		public Target target = null;
		public ModuleMap.Entry entry = null;
		
		public bool visited = false;
		public List<Node> imports_nodes = new List<Node>();
		public List<Node> imported_by_nodes = new List<Node>();
	}
	
	Dictionary<string, Node> module_to_node = new Dictionary<string, Node>();
	Target root_target = null;
	List<Target> all_targets = new List<Target>();
	
	delegate T MeasureFunc<T>();
	T CallMeasured<T>(string measure_message, MeasureFunc<T> f) {
		if(measure_message == null)
			return f();
		var sw = new Stopwatch();
		sw.Start();
		T ret = f();
		sw.Stop();
		Log.LogMessage(MessageImportance.High, "{0} finished in {1}s", measure_message, sw.ElapsedMilliseconds / 1000.0 );
		return ret;
	}
	
	bool ExecuteTask(Task t, string measure_message = null) {
		return CallMeasured<bool>(measure_message, () => {
			return t.Execute();
		});
	}
	
	bool GetNodes(ItemSet itemset)
	{
		// todo: try to keep the whole project tree in memory 
		// rather than reloading the subtrees over and over again during a build
		// todo: maybe parallelize this ?
		// todo: look for import cycles!
		foreach(var target in all_targets) {
			// we expect the module map to be up to date at this point
			ModuleMap module_map = Deserialize<ModuleMap>(target.int_dir + ModuleMapFile);
			foreach(ModuleMap.Entry entry in module_map.entries) {
				var node = new Node {
					target = target,
					entry = entry
				};
				// imports_nodes and imported_by_nodes will be filled in later if needed
				if(entry.exported_module != "") {
					if(module_to_node.ContainsKey(entry.exported_module)) {
						Log.LogMessage(MessageImportance.High, "duplicate module {0}", entry.exported_module);
						return false;
					}
					module_to_node.Add(entry.exported_module, node);
				}
				//this might not be loaded for files in other projects
				if(target == root_target) // todo: check that instead
					itemset.Get(entry.source_file).node = node;
				target.nodes.Add(node);
			}
		}
		
		return true;
	}
	
	class ModuleNotFoundException : System.Exception {
		
	}
	
	// todo: use visitor colors ?
	void ClearVisited(Node node) {
		if(!node.visited)
			return;
		node.visited = false;
		foreach(Node imported_node in node.imports_nodes)
			ClearVisited(imported_node);
	}
	
	string GetSingleModuleReference(Node node) {
		if(node.entry.cmi_file == "")
			return "";
		string prefix = "";
		if(IsLLVM()) {
			prefix = "-Xclang \"-fmodule-file=";
			if(!node.entry.importable_header)
				prefix += node.entry.exported_module + "=";
		} else {
			prefix = "/module:reference \"";
		}
		return prefix + node.entry.cmi_file + "\" ";
	}
	
	string GetModuleReferences(Node node, bool root) {
		if(node.visited)
			return "";
		node.visited = true;
		string ret = root ? "" : GetSingleModuleReference(node);
		foreach(Node imported_node in node.imports_nodes)
			ret += GetModuleReferences(imported_node, false);
		return ret;
	}
	
	string NinjaEscape(string str) {
		return str.Replace("$","$$").Replace(" ", "$ ").Replace(":","$:");
	}
	
	// https://stackoverflow.com/questions/478826/c-sharp-filepath-recasing
	static string GetProperDirectoryCapitalization(System.IO.DirectoryInfo dirInfo)
	{
		var parentDirInfo = dirInfo.Parent;
		if (null == parentDirInfo)
			return dirInfo.Name;
		return Path.Combine(GetProperDirectoryCapitalization(parentDirInfo),
							parentDirInfo.GetDirectories(dirInfo.Name)[0].Name);
	}

	static string GetProperFilePathCapitalization(string filename)
	{
		FileInfo fileInfo = new FileInfo(filename);
		var dirInfo = fileInfo.Directory;
		return Path.Combine(GetProperDirectoryCapitalization(dirInfo),
							dirInfo.GetFiles(fileInfo.Name)[0].Name);
	}
	
	// fix clang warning : non-portable path to file .. specified path differs in case from file name on disk
	string PortablePath(string str) {
		return GetProperFilePathCapitalization(str).Replace("\\", "/");
	}
	
	void GenerateNinjaForNode(Node node, bool only_current)
	{
		if(node.visited)
			return;
		node.visited = true;
		
		foreach(string import in node.entry.imported_modules) {
			Node import_node = null;
			if(!module_to_node.TryGetValue(import, out import_node)) {
				Log.LogMessage(MessageImportance.High, "imported module '{0}' not found in global module map", import);
				throw new ModuleNotFoundException();
			}
			// downward edges are needed in the whole subtree for the references generation
			node.imports_nodes.Add(import_node);
			GenerateNinjaForNode(import_node, only_current);
		}
		
		// we may not need to create rules for files in other projects
		if(only_current && node.target != root_target)
			return;
		
		bool is_module = (node.entry.exported_module != "");
		string outputs = "";
		if(is_module) {
			if(IsLLVM()) // for LLVM we generate the cmi file first
				outputs = NinjaEscape(node.entry.cmi_file);
			else
				outputs = NinjaEscape(node.entry.obj_file) + " " + NinjaEscape(node.entry.cmi_file);
		} else {
			outputs = NinjaEscape(node.entry.obj_file);
		}
		
		var inputs = String.Join(" ", node.imports_nodes.Select(n => n.entry.cmi_file)
			.Append(node.entry.mdef_file).Select(f => NinjaEscape(f)));
		
		ClearVisited(node);
		string base_command = node.entry.base_command + " " + GetModuleReferences(node, true);
		string command = base_command + GetModuleArgsForNode(node);
		
		node.target.ninja += String.Format("build {0}: cc {1}\n cmd = {2}\n", outputs, inputs, command);
		if(is_module && IsLLVM()) {
			outputs = NinjaEscape(node.entry.obj_file);
			node.target.ninja += String.Format("build {0}: cc {1}\n cmd = {2}\n", outputs, inputs, base_command);
		}
		// todo: add cmi hashing, maybe use tracker.exe as well ?
	}
	
	bool GenerateNinjaFiles(bool only_current) {
		// generate a build file to build only the current project or the entire subtree
		// todo: try not to regenerate the whole thing on every build
		var gen_for = !only_current ? all_targets :
			new List<Target>{root_target};
		foreach(var map in gen_for)
			map.ninja = "rule cc\n command = $cmd\n";
		try {
			foreach(var node in root_target.nodes)
				GenerateNinjaForNode(node, only_current);
		} catch(ModuleNotFoundException) {
			return false;
		}
		foreach(var map in gen_for)
			File.WriteAllText(map.int_dir + NinjaBuildFile, map.ninja);
		// generate another build file that builds both the current project,
		// as well as all of its transitively referenced projects
		File.WriteAllLines(IntDir + NinjaRecursiveBuildFile, all_targets.Select(
			map => "subninja " + NinjaEscape(map.int_dir + NinjaBuildFile)));
		return true;
	}
	
	string GetModuleArgsForNode(Node node) {
		string args = "";
		if(IsLLVM()) {
			if(node.entry.exported_module != "") {
				if(node.entry.importable_header)
					args += "-Xclang -emit-header-module " +
						"-Xclang -fmodule-name=" + node.entry.exported_module + " ";
				else
					args += "-Xclang -emit-module-interface ";
				args += "-o \"" + node.entry.cmi_file + "\" ";
			}
		} else {
			if(node.entry.exported_module != "") {
				if(node.entry.importable_header)
					args += "/module:export /module:name " + node.entry.exported_module + " ";
				else
					args += "/module:interface ";
				args += "/module:output \"" + node.entry.cmi_file + "\" ";
				// AdditionalOptions already contains /modules:stdifcdir
			}
		}
		//note: we assume modules are already enabled and the standard is set to latest
		return args;
	}
	
	string GetSelectedNinjaTargets(ItemSet itemset)
	{
		var targets = String.Join("\" \"", CL_Input_Manually_Selected.Select(i => {
			var entry = itemset.Get(i.GetMetadata("Identity")).node.entry;
			string ret = entry.obj_file;
			if(IsLLVM() && entry.exported_module != "")
				ret += "\" \"" + entry.cmi_file;
			return ret;
		}));
		return targets == "" ? "" : "\"" + targets + "\"";
	}
	
	bool Compile(ItemSet itemset, bool only_current)
	{
		Log.LogMessage(MessageImportance.High, "CppM: Compile");
		
		if(!GetNodes(itemset)) {
			Log.LogMessage(MessageImportance.High, "failed to read global module map");
			return false;
		}
		
		if(!GenerateNinjaFiles(only_current))
			return false;

		// todo: pass -j N to ninja based on the VS settings
		var ninja_file = IntDir + (only_current ? NinjaBuildFile : NinjaRecursiveBuildFile);
		var command = String.Format("\"{0}\" -f \"{1}\" {2}", 
			NinjaExecutablePath, ninja_file, GetSelectedNinjaTargets(itemset));
		var exec_res = ExecuteCommand(command, measure_message: "ninja");
		if(exec_res.exit_code != 0) {
			Log.LogMessage(MessageImportance.High, "ERROR: compilation failed");
			return false;
		}
		return true;
	}
	
	// build this project and all of its transitively referenced projects
	bool BuildEverything() 
	{
		Log.LogMessage(MessageImportance.High, "building project subtree");
		
		// collect all the itemset from the transitively referenced projects
		// try to support any other msbuild customizations that affect the item sets needed for the build
		
		// todo: the same source file could appear in multiple projects
		var itemset = new ItemSet();
		foreach(var item in CL_Input_All)
			itemset.Add(item, root_target);
			
		// note: the maps in all_targets are in BFS order
		// so traversing that in reverse order ensures each project is built before its references
		foreach(var target in Enumerable.Reverse(all_targets)) {
			// we've already added the items for the current project, don't build it again
			if(target == root_target) continue;
			//var path = target.project.GetPropertyValue("MSBuildProjectFullPath");
			Log.LogMessage(MessageImportance.High, "building {0}", target.project.FullPath);
			var instance = target.project.CreateProjectInstance(); // project is immutable, instance is not
			if(!instance.Build("CppM_PreProcess", null)) // todo: build up to ClCompile to be sure we get the right config ?
				throw new System.Exception("failed to build project");
			var local_items = instance.GetItems("CppM_CL_Input_All"); // todo: or selected ?
			foreach(var item in local_items)
				itemset.Add(item, target);
			
			//get the set of impotable headers and generate a header unit module map for this particular project
			//var all_importable_headers = GetImportableHeaders(project);
		}
		
		ReadTLogs_GetOOD(ref itemset);
		//Log.LogMessage(MessageImportance.High, "nr items: {0}, nr projects {1}", itemset.Items.Count(), 
		// todo: should be different importable headers for each project
		all_importable_headers_tmp = GetImportableHeaders();
		return PreProcess(itemset) && Compile(itemset, only_current: false);
	}
	
	bool BuildCurrentProject() {
		all_importable_headers_tmp = GetImportableHeaders();
		var itemset = ReadTLogs_GetOOD();
		return PreProcess(itemset) && Compile(itemset, only_current: true);
	}

	public override bool Execute()
	{
		if(PreProcess_Only == "true") return true;
		
		CreateProjectCollection("creating project collection");
		
		// unless some specific files/projects are manually selected (to build only that)
		// the references of this project are assumed to have been built already
		// (either by the IDE, or by passing BuildProjectReferences=true to MSBuild)
		if(CL_Input_Manually_Selected.Length > 0 && ProjectReference.Length > 0)
			return BuildEverything();
		else
			return BuildCurrentProject();
	}
}