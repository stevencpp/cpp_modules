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
using System.Collections.Generic; // for List<T>, Dictionary<T,U>, HashSet<T>, IEnumerable<T>, LinkedList<T>
using PropertyInfo = System.Reflection.PropertyInfo;
using FileInfo = System.IO.FileInfo;
using File = System.IO.File;
using Path = System.IO.Path;
using Directory = System.IO.Directory;
using Stopwatch = System.Diagnostics.Stopwatch;

using System.Linq; // for Select, Where, SelectMany

public class CppM_CL : Task
{
	[Required] public ITaskItem[] CL_Input_All { get; set; }
	[Required] public ITaskItem[] CL_Input_Selected { get; set; }
	[Required] public ITaskItem[] CL_Input_Manually_Selected { get; set; }
	[Required] public ITaskItem[] CurrentProjectHolder { get; set; }
	[Required] public ITaskItem[] ProjectReference { get; set; }
	[Required] public String ModuleMapFile { get; set; }
	[Required] public String SolutionFilePath { get; set; }
	[Required] public String Configuration { get; set; }
	[Required] public String Platform { get; set; }
	[Required] public String PlatformToolset { get; set; }
	[Required] public String PreProcess_Only { get; set; }
	[Required] public String BMI_Path { get; set; }
	[Required] public String BMI_Ext { get; set; }
	[Required] public String ClangScanDepsPath { get; set; }
	[Required] public String PP_CompilationDatabase { get; set; }
	[Required] public String IntDir { get; set; }
	[Required] public String NinjaExecutablePath { get; set; }
	
	string NinjaBuildFile(string int_dir) { return int_dir + "build.ninja"; }
	string NinjaRecursiveBuildFile(string int_dir) { return int_dir + "build_rec.ninja"; }
	
	ITaskItem[] CL_Input_OutOfDate_PP = null;
	ITaskItem[] CL_Input_OutOfDate_Src = null;
	
	String CurrentProject = "";
	ProjectCollection project_collection = null;
	
	// todo: replace this with a compiler enum
	bool IsLLVM() {
		var toolset = PlatformToolset.ToLower();
		return toolset == "llvm" || toolset == "clangcl";
	}
	
	// load all transitively referenced projects
	// todo: keep this in memory somehow when building a project hierarchy
	bool CreateProjectCollection() {
		CurrentProject = CurrentProjectHolder[0].GetMetadata("FullPath");
		
		project_collection = new ProjectCollection( new Dictionary<String, String> {
			{ "SolutionDir", Path.GetDirectoryName(SolutionFilePath) + "\\" },
			{ "Configuration", Configuration }, { "Platform", Platform },
			{ "SolutionPath", SolutionFilePath } // just for completeness
		});
		
		var project_queue = new Queue<string>();
		var loaded_projects = new HashSet<string>();
		project_queue.Enqueue(CurrentProject);
		loaded_projects.Add(CurrentProject);
		
		while(project_queue.Count > 0) {
			var project_file = project_queue.Dequeue();
			var project = project_collection.LoadProject(project_file);
			
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
	
	HashSet<string> all_importable_headers = null;
	
	bool GetImportableHeaders()
	{
		all_importable_headers = project_collection.LoadedProjects.SelectMany(project =>
			project.GetItems("ClCompile")
				.Where(i => i.GetMetadataValue("CppM_Header_Unit") == "true"
					&& i.GetMetadataValue("ExcludedFromBuild") != "true")
				.Select(i => NormalizeFilePath(i.GetMetadataValue("FullPath")))
		).ToHashSet();
		all_importable_headers.Remove("");
		
		return true;
	}
	
	string NormalizeFilePath(string file_path) {
		try { return Path.GetFullPath(file_path).ToUpper(); }
		catch (System.Exception e) {
			Log.LogMessage(MessageImportance.High, "failed to normalize path {0}", file_path);
			throw e;
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
	
	void InitTLogs(Dictionary<string, TLogSet> src_to_tlog_set, IEnumerable<string> sources) {
		foreach(string src_file in sources) {
			src_to_tlog_set.Add(src_file, new TLogSet{ src_file = src_file });
		}
	}
	
	void ReadTLogs(Dictionary<string, TLogSet> src_to_tlog_set, string directory)
	{
		for(int i = 0; i < 3; i++) {
			TLogSet current_set = null;
			bool skip = true;
			try {
				foreach(string line in File.ReadAllLines(directory + "cppm." + rws[i] + ".1.tlog")) {
					if(line.StartsWith("^")) {
						skip = true;
						string src_file = NormalizeFilePath(line.Substring(1));
						// only read the tlogs if the source file still exists (TLogSet was initialized)
						if(src_to_tlog_set.TryGetValue(src_file, out current_set)) {
							// don't read the old tlogs if newer ones have already been loaded
							skip = current_set.GetLines(i).Count > 0;
						}
					} else if(!skip) {
						current_set.GetLines(i).Add(i == 2 ? line : NormalizeFilePath(line));
					}
				}
			} catch(System.IO.FileNotFoundException) {
				// it's ok, if it doesn't exist just leave this part of the TLogSet empty
				//Log.LogMessage(MessageImportance.High, "could not read {0}", directory + "cppm." + rws[i] + ".1.tlog");
			}
		}
	}
	
	void WriteTLogs(Dictionary<string, TLogSet> src_to_tlog_set, string directory) {
		for(int i = 0; i < 3; i++) {
			File.WriteAllLines(
				directory + "cppm." + rws[i] + ".1.tlog",
				src_to_tlog_set.Values.SelectMany(tlog_set => tlog_set.GetOutputLines(i)).ToArray()
			);
		}
	}
	
	void PrintAllMetadata(ITaskItem item) {
		foreach(var meta in item.MetadataNames) {
			try {
				Log.LogMessage(MessageImportance.High, "meta {0} = {1}", meta.ToString(), item.GetMetadata(meta.ToString()) );
			} catch(System.Exception) {}
		}
	}
	
	delegate string PropToValueFunc(string prop);
	
	string GetBaseCommand(ITaskItem item, bool preprocess) {
		return GetBaseCommand(prop => item.GetMetadata(prop), preprocess);
	}
	
	string GetBaseCommand(Dictionary<string, string> cl_params, bool preprocess) {
		return GetBaseCommand(prop => {
			string meta_value = null;
			return cl_params.TryGetValue(prop, out meta_value) ? meta_value : "";
		}, preprocess);
	}
	
	string GetBaseCommand(PropToValueFunc p_to_v, bool preprocess)
	{
		var cmdline = new CLCommandLine();
		foreach(string prop in props) {
			// this makes sure the command doesn't change when we call msbuild on other projects for preprocessing:
			if(props_not_for_cmd.Contains(prop)) continue;
			string meta_value = p_to_v(prop);
			if(meta_value != "" && !SetPropertyFromString(cmdline, prop, meta_value)) {
				Log.LogMessage(MessageImportance.High, "failed to set {0}", prop);
				throw new System.Exception("failed to set property for CLCommandLine");
			}
		}
		if(preprocess) {
			cmdline.PreprocessToFile = true;
			cmdline.EnableModules = false; // for clang-scan-deps
			cmdline.AdditionalOptions = ""; // todo: just remove /module:stdIfcDir
		}
		if(!cmdline.Execute())
			throw new System.Exception("failed to execute CLCommandLine");
		string tool = cmdline.ToolExe;
		if(!IsLLVM()) // for some reason CLToolExe no longer seems to bet set for MSVC
			tool = "cl.exe";
		string args = cmdline.CommandLines[0].GetMetadata("Identity");
		var name = PortablePath(NormalizeFilePath(cmdline.Sources[0].GetMetadata("FullPath")));
		return String.Format("\"{0}\" {1} \"{2}\"", tool, args, name);
	}
	
	ITaskItem[] GetOOD(string tlog_directory, Dictionary<string, TLogSet> src_to_tlog_set, bool preprocess)
	{
		var get_ood_sources = new List<TaskItem>();
		foreach(ITaskItem item in CL_Input_All) {
			var tlog_set = src_to_tlog_set[NormalizeFilePath(item.GetMetadata("FullPath"))];
			tlog_set.SetCommand(GetBaseCommand(item, preprocess));
			var ood_item = new TaskItem(tlog_set.src_file);
			ood_item.SetMetadata("write", String.Join(";", tlog_set.outputs));
			ood_item.SetMetadata("read", String.Join(";", tlog_set.inputs));
			ood_item.SetMetadata("command", tlog_set.GetCommand());
			//Log.LogMessage(MessageImportance.High, "{0} - {1} - dir {2}", item.GetMetadata("FileName"), tlog_set.GetCommand(), tlog_directory);
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
	
	bool ReadTLogs_GetOOD()
	{
		if(CL_Input_All.Length == 0)
			return true;
		
		string tlog_dir = CL_Input_All[0].GetMetadata("TrackerLogDirectory");
		string tlog_dir_pp = tlog_dir + "pp\\";
		src_to_tlog_set_pp = new Dictionary<string, TLogSet>();
		InitTLogs(src_to_tlog_set_pp, CL_Input_All.Select(
			item => NormalizeFilePath(item.GetMetadata("FullPath"))
		));
		ReadTLogs(src_to_tlog_set_pp, tlog_dir_pp);
		CL_Input_OutOfDate_PP = GetOOD(tlog_dir_pp, src_to_tlog_set_pp, true);
		return true;
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
	
	Dictionary<string, string> Get_CL_PropertyDictionary(ITaskItem item)
	{
		var dict = new Dictionary<string, string>();
		foreach(string prop in props) {
			string meta_value = item.GetMetadata(prop);
			if(meta_value != "")
				dict.Add(prop, meta_value);
		}
		return dict;
	}
	
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
		public string bmi_file = "";
		public string exported_module = "";
		public bool importable_header = false;
		public List<string> imported_modules = new List<string>();
		public List<string> imported_headers = new List<string>();
		public List<string> included_headers = new List<string>();
		public Dictionary<string, string> cl_params = new Dictionary<string, string>();
		
		public void assign(ModuleDefinition def) {
			bmi_file = def.bmi_file;
			exported_module = def.exported_module;
			importable_header = def.importable_header;
			imported_modules = def.imported_modules;
			imported_headers = def.imported_headers;
			included_headers = def.included_headers;
			cl_params = def.cl_params;
		}
	}
	
	public class ModuleMap
	{
		public class Entry : ModuleDefinition {
			public string source_file = "";
			
			public static Entry create(string src_file, ModuleDefinition module_def) {
				Entry ret = new Entry { source_file = src_file };
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
	
	class ExecuteResult { public bool success, got_exit_code; public int exit_code; public ITaskItem[] console_output; }
	ExecuteResult ExecuteCommand(string command, bool pipe_output)
	{
		Log.LogMessage(MessageImportance.High, "executing {0}", command);
		var exec = new Exec {
			Command = command,
			ConsoleToMSBuild = pipe_output,
			BuildEngine = this.BuildEngine,
			EchoOff = pipe_output,
			IgnoreExitCode = true,
			StandardOutputImportance = pipe_output ? "low" : "high"
		};
		bool ok = exec.Execute();
		if(!ok)	Log.LogMessage(MessageImportance.High, "ERROR: failed to execute {0}", command);
		return new ExecuteResult { success = (ok && exec.ExitCode == 0), got_exit_code = ok, 
			exit_code = exec.ExitCode, console_output = exec.ConsoleOutput };
	}
	
	void LogScanReport(string src_file, ModuleDefinition mdef) {
		string report = "";
		if(mdef.exported_module != "" && !mdef.importable_header)
			report = "exp: " + mdef.exported_module + " ";
		string imports = String.Join(" ", mdef.imported_modules) +
			String.Join(" ", mdef.imported_headers);
		report += (imports != "" ? "imp: " + imports : "");
		if(report != "") Log.LogMessage(MessageImportance.High, "{0} {1}", src_file, report);
	}
	
	bool RunScanDeps(ModuleMap module_map, HashSet<string> ood_files, Dictionary<string, ITaskItem> src_to_item)
	{
		// create a preprocessor specific compilation database that only includes out of date files
		// and does not ask the compiler to load any modules
		//Log.LogMessage(MessageImportance.High, "generating preprocessor compilation database");
		var compilation_database = new List<CompilationDatabaseEntry>();
		foreach(var entry in src_to_tlog_set_pp) {
			var name = entry.Key; var tlog_set = entry.Value;
			if(!ood_files.Contains(name)) continue;
			//Log.LogMessage(MessageImportance.High, "cmd {0}", cmd);
			compilation_database.Add(new CompilationDatabaseEntry {
				file = name, command = tlog_set.GetCommand() // todo: directory ???
			});
		}
		SerializeToFile(compilation_database, PP_CompilationDatabase);
		
		var command = String.Format("\"{0}\" --compilation-database=\"{1}\"", 
			ClangScanDepsPath, PP_CompilationDatabase);
		//Log.LogMessage(MessageImportance.High, "cmd = {0}", command);
		var exec_res = ExecuteCommand(command, true);
		if(!exec_res.success) {
			if(exec_res.got_exit_code) Log.LogMessage(MessageImportance.High, "ERROR: failed to preprocess the sources");
			return false;
		}
		
		var remaining_src = ood_files.ToHashSet();
		ModuleDefinition cur_mdef = null;
		string cur_src_file = "";
		ITaskItem cur_item = null;
		TLogSet cur_tlog_set = null;
		VoidFunc finish_cur_mdef = () => {
			if(cur_mdef == null) return;
			if(cur_mdef.exported_module != "") // could be from a named module or imported header
				cur_mdef.bmi_file = BMI_Path + cur_mdef.exported_module + BMI_Ext; // todo: use CppM_BMI_File ?
			string def_file = cur_item.GetMetadata("CppM_ModuleDefinitionFile").ToString();
			cur_tlog_set.outputs.Clear();
			cur_tlog_set.outputs.Add(NormalizeFilePath(def_file));
			SerializeToFile(cur_mdef, def_file);
			var entry_to_add = ModuleMap.Entry.create(cur_src_file, cur_mdef);
			module_map.entries.Add(entry_to_add);
			remaining_src.Remove(cur_src_file);
			LogScanReport(cur_src_file, cur_mdef);
			cur_mdef = null;
		};
		int i = 0;
		foreach(var line_item in exec_res.console_output) {
			if(i++ == 0) continue;
			var line = line_item.GetMetadata("Identity");
			//Log.LogMessage(MessageImportance.High, "{0}", line);
			//continue;
			if(line.StartsWith(":::: ")) {
				// source file
				finish_cur_mdef();
				cur_src_file = NormalizeFilePath(line.Substring(5));
				if(!src_to_item.TryGetValue(cur_src_file, out cur_item)) {
					Log.LogMessage(MessageImportance.High, "ERROR: source file {0} not among the inputs", cur_src_file);
					return false;
				}
				cur_tlog_set = src_to_tlog_set_pp[cur_src_file];
				cur_tlog_set.inputs.Clear();
				cur_tlog_set.inputs.Add(ClangScanDepsPath);
				// note: clang-scan-deps doesn't execute a separate compiler tool

				cur_mdef = new ModuleDefinition {
					cl_params = Get_CL_PropertyDictionary(cur_item),
					importable_header = (cur_item.GetMetadata("CppM_Header_Unit") == "true"),
				};
				if(cur_mdef.importable_header)
					cur_mdef.exported_module = GetImportableHeaderModuleName(cur_src_file);
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
				if(line == ":exp") continue; // todo: fix scanner bug
				// other dependencies (headers,modulemap,precompiled header bmis)
				var file = NormalizeFilePath(line);
				if(file == cur_src_file) // for OOD detection
					continue;
				cur_tlog_set.inputs.Add(file);
				if(all_importable_headers.Contains(file)) { // todo: shouldn't need to check, the scanner should tell us
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
		string tlog_dir = CL_Input_All[0].GetMetadata("TrackerLogDirectory");
		string tlog_dir_pp = tlog_dir + "pp\\";
		WriteTLogs(src_to_tlog_set_pp, tlog_dir_pp);
		
		if(remaining_src.Count > 0) {
			//foreach(var line_item in exec_res.console_output) // todo: weird, if this is uncommented, the errors below don't show up
			//	Log.LogMessage(MessageImportance.High, "{0}", line_item.GetMetadata("Identity"));
			foreach(var src_file in remaining_src)
				Log.LogMessage(MessageImportance.High, "ERROR: source file {0} was not successfully preprocessed", src_file);
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

	bool PreProcess()
	{
		Log.LogMessage(MessageImportance.High, "CppM: PreProcess");
		
		var ood_files = CL_Input_OutOfDate_PP.Select(item => 
			NormalizeFilePath(item.GetMetadata("FullPath"))).ToHashSet();
		var src_to_item = CL_Input_All.ToDictionary(item => 
			NormalizeFilePath(item.GetMetadata("FullPath")));

		ModuleMap module_map = new ModuleMap();
		
		// scan out of date files for dependencies, add them to the module map
		if(ood_files.Count > 0) {
			if(!RunScanDeps(module_map, ood_files, src_to_item))
				return false;
		}

		// even if all of the files up to date, it's still possible
		// for the module map to be out of date, e.g if a file is removed from the project
		if(ood_files.Count > 0 || IsProjectDirty(CurrentProject, ModuleMapFile))
		{
			var old_module_map = DeserializeIfExists<ModuleMap>(ModuleMapFile);
			var old_src_to_entry = GetModuleMap_SrcToEntry(old_module_map);
			foreach(var entry in src_to_item) {
				var src_file = entry.Key; var item = entry.Value;
				if(ood_files.Contains(src_file)) continue;
				ModuleMap.Entry entry_to_add = null;
				string def_file = item.GetMetadata("CppM_ModuleDefinitionFile").ToString();
				if(old_module_map != null && NewerThan(ModuleMapFile, def_file)) {
					if(!old_src_to_entry.TryGetValue(src_file, out entry_to_add)) {
						Log.LogMessage(MessageImportance.High, "ERROR: module map {0} created after " + 
							"definition file {1}, but does not contain an entry for " + 
							"source file {2} ??", ModuleMapFile, def_file, src_file);
						return false;
					}
				} else {
					var module_def = DeserializeIfExists<ModuleDefinition>(def_file);
					entry_to_add = ModuleMap.Entry.create(src_file, module_def);
				}
				module_map.entries.Add(entry_to_add);
			}
			
			SerializeToFile(module_map, ModuleMapFile);
		} else {
			File.SetLastWriteTimeUtc(ModuleMapFile, System.DateTime.UtcNow);
		}
		//return false;
		//Log.LogMessage(MessageImportance.High, "test: {0}", serializer.Serialize(module_map));
		
		return true;
	}
	
	public class GlobalModuleMap
	{
		public ModuleMap map = null;
		public Project project = null;
	}
	
	public class GlobalModuleMapNode
	{
		public GlobalModuleMap map = null;
		public ModuleMap.Entry entry = null;
		
		public bool visited = false;
		public List<GlobalModuleMapNode> imports_nodes = new List<GlobalModuleMapNode>();
		public List<GlobalModuleMapNode> imported_by_nodes = new List<GlobalModuleMapNode>();
	}
	
	Dictionary<string, GlobalModuleMapNode> global_module_to_node = new Dictionary<string, GlobalModuleMapNode>();
	Dictionary<string, GlobalModuleMapNode> local_source_to_node = new Dictionary<string, GlobalModuleMapNode>();
	List<GlobalModuleMap> all_global_module_maps = new List<GlobalModuleMap>();
	Dictionary<string, TLogSet> src_to_tlog_set_pp = new Dictionary<string, TLogSet>();
	
	bool ExecuteTask(Task t, string measure_message = null) {
		if(measure_message == null)
			return t.Execute();
		var sw = new Stopwatch();
		sw.Start();
		bool ret = t.Execute();
		sw.Stop();
		Log.LogMessage(MessageImportance.High, "{0} finished in {1}s", measure_message, sw.ElapsedMilliseconds / 1000.0 );
		return ret;
	}
	
	bool GetGlobalModuleMap()
	{
		// unless some specific files/projects are manually selected (to build only that)
		// the references of this project are assumed to have been built already
		// (either by the IDE, or by passing BuildProjectReferences=true to MSBuild)
		if(CL_Input_Manually_Selected.Length > 0 && ProjectReference.Length > 0) {
			// todo: use the projectcollection's already loaded projects to improve performance
			Stack<String> properties = new Stack<String>();
			properties.Push("BuildingInsideVisualStudio=false");
			properties.Push("CppM_PreProcess_Only=true");
			properties.Push("BuildProjectReferences=true");
			var build = new MSBuild {
				BuildEngine = this.BuildEngine,
				Properties = properties.ToArray(),
				Projects = ProjectReference,
				Targets = new String[] { "ClCompile" }
			};
			Log.LogMessage(MessageImportance.High, "preprocessing project subtree");
			if(!ExecuteTask(build, "preprocessing project subtree"))
				return false;
			Log.LogMessage(MessageImportance.High, "compiling");
		}
		
		var project_source_to_node = new Dictionary<string, GlobalModuleMapNode>();
		
		// todo: try to keep the whole project tree and the global module map in memory 
		// rather than reloading the subtrees over and over again during a build
		// todo: maybe parallelize this ?
		foreach(Project project in project_collection.LoadedProjects) {
			var project_file = project.FullPath;
			
			string module_map_file = project.GetPropertyValue("CppM_ModuleMapFile");
			// not an error if modules are not enabled in the project
			if(module_map_file == "")
				continue;
			ModuleMap module_map = DeserializeIfExists<ModuleMap>(module_map_file);
			// we expect the module map to be up to date at this point
			if(module_map == null) {
				Log.LogMessage(MessageImportance.High, "ERROR: module map '{0}' does not exist", module_map_file);
				return false;
			}
				
			var global_map = new GlobalModuleMap{ map = module_map, project = project };
			all_global_module_maps.Add(global_map);
			
			var source_to_node = (project_file == CurrentProject ? local_source_to_node : project_source_to_node);
			source_to_node.Clear();
			
			foreach(ModuleMap.Entry entry in module_map.entries) {
				var node = new GlobalModuleMapNode {
					map = global_map,
					entry = entry
				};
				// imports_nodes and imported_by_nodes will be filled in later if needed
				
				if(entry.exported_module != "") {
					if(global_module_to_node.ContainsKey(entry.exported_module)) {
						Log.LogMessage(MessageImportance.High, "duplicate module {0}", entry.exported_module);
						return false;
					}
					global_module_to_node.Add(entry.exported_module, node);
				}
				source_to_node.Add(entry.source_file, node);
			}
		}
		
		return true;
	}
	
	class ModuleNotFoundException : System.Exception {
		
	}
	
	// todo: use visitor colors ?
	void ClearVisited(GlobalModuleMapNode node) {
		if(!node.visited)
			return;
		node.visited = false;
		foreach(GlobalModuleMapNode imported_node in node.imports_nodes)
			ClearVisited(imported_node);
	}
	
	string GetSingleModuleReference(GlobalModuleMapNode node) {
		if(node.entry.bmi_file == "")
			return "";
		string prefix = !IsLLVM() ? "/module:reference \"" : (
			"-Xclang \"-fmodule-file=" + node.entry.exported_module + "=");
		return prefix + node.entry.bmi_file + "\" ";
	}
	
	string GetModuleReferences(GlobalModuleMapNode node, bool root) {
		if(node.visited)
			return "";
		node.visited = true;
		string ret = root ? "" : GetSingleModuleReference(node);
		foreach(GlobalModuleMapNode imported_node in node.imports_nodes)
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
		return !IsLLVM() ? str : GetProperFilePathCapitalization(str).Replace("\\", "/");
	}
	
	void GenerateNinjaForNode(GlobalModuleMapNode node, ref string ninja)
	{
		if(node.visited)
			return; // if already visited then don't add additional references
		node.visited = true;
		
		foreach(string import in node.entry.imported_modules) {
			GlobalModuleMapNode import_node = null;
			if(!global_module_to_node.TryGetValue(import, out import_node)) {
				Log.LogMessage(MessageImportance.High, "imported module '{0}' not found in global module map", import);
				throw new ModuleNotFoundException();
			}
			// downward edges are needed in the whole subtree for the references generation
			node.imports_nodes.Add(import_node);
			GenerateNinjaForNode(import_node, ref ninja);
		}
		
		string src_file = node.entry.source_file;
		// we don't need to create rules for files in other projects
		if(!src_to_tlog_set_pp.ContainsKey(src_file))
			return;
		
		bool is_module = (node.entry.bmi_file != "");
		string object_file_path = node.entry.cl_params["ObjectFileName"] + Path.GetFileNameWithoutExtension(src_file) + ".obj";
		string outputs = "";
		if(is_module) {
			if(IsLLVM()) // for LLVM we generate the bmi file first
				outputs = NinjaEscape(node.entry.bmi_file);
			else
				outputs = NinjaEscape(object_file_path) + " " + NinjaEscape(node.entry.bmi_file);
		} else {
			outputs = NinjaEscape(object_file_path);
		}
		
		var tlog_set_pp = src_to_tlog_set_pp[src_file]; // todo: add a field to the node ?
		var inputs = NinjaEscape(src_file) + " " + 
			String.Join(" ", node.imports_nodes.Select(n => NinjaEscape(n.entry.bmi_file))) + " " +
			String.Join(" ", tlog_set_pp.inputs.Select(i => NinjaEscape(i)));
		
		ClearVisited(node);
		// todo: store the command in the entry instead of generating it again?
		string base_command = GetBaseCommand(node.entry.cl_params, false) + " " + 
			GetModuleReferences(node, true);
		string command = base_command + GetModuleArgsForNode(node);
		
		ninja += String.Format("build {0}: cc {1}\n cmd = {2}\n", outputs, inputs, command);
		if(is_module && IsLLVM()) {
			outputs = NinjaEscape(object_file_path);
			ninja += String.Format("build {0}: cc {1}\n cmd = {2}\n", outputs, inputs, base_command);
		}
		// todo: add cmi hashing
	}
	
	bool GenerateNinjaFiles() {
		string contents = "rule cc\n command = $cmd\n";
		try {
			foreach(var entry in local_source_to_node)
				GenerateNinjaForNode(entry.Value, ref contents);
		} catch(ModuleNotFoundException) {
			return false;
		}
		File.WriteAllText(NinjaBuildFile(IntDir), contents);
		
		// the loaded project collection already contains the set of transitively referenced projects
		contents = "subninja " + NinjaEscape(NinjaBuildFile(IntDir)) + "\n";
		var current_project = project_collection.LoadProject(CurrentProject);
		foreach(Project project in project_collection.LoadedProjects) {
			var project_file = project.FullPath;
			if(project_file == CurrentProject) continue;
			var int_dir = project.GetPropertyValue("CppM_IntDir_FullPath");
			if(int_dir == "") continue; // e.g for the cmake generated ZERO_CHECK project
			contents += String.Format("subninja {0}\n", NinjaEscape(NinjaBuildFile(int_dir)));
		}
		File.WriteAllText(NinjaRecursiveBuildFile(IntDir), contents);
		return true;
	}
	
	string GetModuleArgsForNode(GlobalModuleMapNode node) {
		string args = "";
		if(IsLLVM()) {
			if(node.entry.exported_module != "") {
				if(node.entry.importable_header)
					args += "-Xclang -emit-header-module " +
						"-Xclang -fmodule-name=" + node.entry.exported_module + " ";
				else
					args += "-Xclang -emit-module-interface ";
				args += "-o \"" + node.entry.bmi_file + "\" ";
			}
		} else {
			if(node.entry.exported_module != "") {
				if(node.entry.importable_header)
					args += "/module:export /module:name " + node.entry.exported_module + " ";
				else
					args += "/module:interface ";
				args += "/module:output \"" + node.entry.bmi_file + "\" ";
				// AdditionalOptions already contains /modules:stdifcdir
			}
		}
		//note: we assume modules are already enabled and the standard is set to latest
		return args;
	}
	
	string GetSelectedNinjaTargets()
	{
		var targets = String.Join("\" \"", CL_Input_Manually_Selected.Select(
			i => local_source_to_node[ NormalizeFilePath(i.GetMetadata("Identity")) ].entry.bmi_file));
		return targets == "" ? "" : "\"" + targets + "\"";
	}
	
	bool Compile()
	{	
		Log.LogMessage(MessageImportance.High, "CppM: Compile");
		
		if(!GetGlobalModuleMap()) {
			Log.LogMessage(MessageImportance.High, "failed to read global module map");
			return false;
		}
		
		if(!GenerateNinjaFiles())
			return false;

		var ninja_file = NinjaBuildFile(IntDir);
		if(CL_Input_Manually_Selected.Length > 0)
			ninja_file = NinjaRecursiveBuildFile(IntDir);
		
		var command = String.Format("\"{0}\" -f \"{1}\" {2}", 
			NinjaExecutablePath, ninja_file, GetSelectedNinjaTargets());
		var exec_res = ExecuteCommand(command, false);
		if(!exec_res.success) {
			if(exec_res.got_exit_code) Log.LogMessage(MessageImportance.High, "ERROR: compilation failed");
			return false;
		}
		return true;
	}

	public override bool Execute()
	{
		return CreateProjectCollection() && 
			GetImportableHeaders() &&
			ReadTLogs_GetOOD() && 
			PreProcess() &&
			(PreProcess_Only == "true" || Compile());
	}
}