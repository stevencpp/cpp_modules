using Task = Microsoft.Build.Utilities.Task;
using TaskItem = Microsoft.Build.Utilities.TaskItem;
using FileTracker = Microsoft.Build.Utilities.FileTracker;

using MessageImportance = Microsoft.Build.Framework.MessageImportance;
using ITaskItem = Microsoft.Build.Framework.ITaskItem;
using Required = Microsoft.Build.Framework.RequiredAttribute;

using ProjectCollection = Microsoft.Build.Evaluation.ProjectCollection;
using Project = Microsoft.Build.Evaluation.Project;
using ProjectProperty = Microsoft.Build.Evaluation.ProjectProperty;
using ProjectItem = Microsoft.Build.Evaluation.ProjectItem;

using CL = Microsoft.Build.CPPTasks.CL;
using GetOutOfDateItems = Microsoft.Build.CPPTasks.GetOutOfDateItems;
using MSBuild = Microsoft.Build.Tasks.MSBuild;

using String = System.String;
using Convert = System.Convert;
using Type = System.Type;
using System.Collections.Generic; // for List<T>, Dictionary<T,U>, HashSet<T>, IEnumerable<T>, LinkedList<T>
using PropertyInfo = System.Reflection.PropertyInfo;
using FileInfo = System.IO.FileInfo;
using File = System.IO.File;
using Path = System.IO.Path;
using Directory = System.IO.Directory;
using Regex = System.Text.RegularExpressions.Regex;
using Match = System.Text.RegularExpressions.Match;

using System.Linq; // for Select, Where, SelectMany

//using JavaScriptSerializer = System.Web.Script.Serialization.JavaScriptSerializer;
//using JavaScriptConverter = System.Web.Script.Serialization.JavaScriptConverter;
//using Activator = System.Activator;

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
	[Required] public String PreProcess_Only { get; set; }
	[Required] public String OutOfDate_File { get; set; }
	[Required] public String BMI_Path { get; set; }
	[Required] public String BMI_Ext { get; set; }
	
	ITaskItem[] CL_Input_OutOfDate_PP = null;
	ITaskItem[] CL_Input_OutOfDate_Src = null;
	
	String CurrentProject = "";
	ProjectCollection project_collection = null;
	
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
	
	IEnumerable<string> legacy_header_paths = null;
	
	// note: part of the workaround until we get a proper legacy header preprocessor
	// this should instead get the set of legacy headers, not paths
	bool GetLegacyHeaderPaths()
	{
		legacy_header_paths = project_collection.LoadedProjects.Select(project => 
			project.GetPropertyValue("CppM_LegacyHeader_Path"));
		return true;
	}
	
	string NormalizeFilePath(string file_path) {
		return Path.GetFullPath(file_path).ToUpper();
	}
	
	// todo: try to use the CL task's existing tracker functionality
	// (currently TrackFileAccess causes a compile error)
	void StartTracking(string directory, string unique_prefix)
	{
		FileTracker.StartTrackingContext(directory, unique_prefix);
	}
	
	void EndTracking(bool success, string directory, string unique_prefix, string src_file, string command)
	{
		FileTracker.WriteContextTLogs(directory, unique_prefix);
		FileTracker.StopTrackingAndCleanup();
		File.WriteAllLines(directory + unique_prefix + ".command.1.tlog", new string[] { "#", command });
		if(success) {
			File.AppendAllLines(directory + "prefixes.txt", new string[] { unique_prefix + " " + NormalizeFilePath(src_file) } );
		}
	}
	
	class TLogSet
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
				//Log.LogMessage(MessageImportance.High, "could not read {0}", directory + "cppm." + rws[i] + ".1.tlog");
			}
		}
	}
	
	Dictionary<string, TLogSet> ReadUpdateTLogs(string directory)
	{
		var src_to_tlog_set = new Dictionary<string, TLogSet>();
		
		InitTLogs(src_to_tlog_set, CL_Input_All.Select(
			item => NormalizeFilePath(item.GetMetadata("FullPath"))
		));
		
		bool cppm_out_of_date = false;
		var to_delete = new List<string>();
		string prefixes_file = directory + "prefixes.txt";
		if(File.Exists(prefixes_file)) {
			var prefix_to_tlog_set = new Dictionary<string, TLogSet>();
			
			foreach(string line in File.ReadAllLines(prefixes_file)) {
				int spc = line.IndexOf(' ');
				string prefix = line.Substring(0, spc);
				string src_file = line.Substring(spc+1);
				TLogSet tlog_set = null;
				// ignore prefixes for sources no longer in the project
				if(!src_to_tlog_set.TryGetValue(src_file, out tlog_set))
					continue;
				prefix_to_tlog_set.Add(prefix, tlog_set);
			}
			foreach(string file_path in Directory.GetFiles(directory, "*.tlog")) {
				string filename = Path.GetFileName(file_path);
				int sep_idx = filename.IndexOfAny(new char[]{'.','-'});
				string prefix = filename.Substring(0, sep_idx);
				if(prefix != "cppm")
					to_delete.Add(file_path);
				TLogSet tlog_set = null;
				if(!prefix_to_tlog_set.TryGetValue(prefix, out tlog_set)) {
					// note: if the previous build included a file that got removed then,
					// the prefix will be ignored but the tlog files still exist
					continue;
				}
				
				string suffix = filename.Substring(sep_idx+1);
				
				int i = 0;
				for(; i < 3; i++) {
					if(suffix.Contains(rws[i]))
						break;
				}
				if(i == 3)
					continue;
				
				var list = tlog_set.GetLines(i);
				// note: there can be multiple .read.tlogs for example
				// todo: use HashSets to make the elements unique
				foreach(string line in File.ReadAllLines(file_path).Skip(1)) {
					list.Add(line);
				}
				cppm_out_of_date = true;
			}
			foreach(TLogSet tlog_set in prefix_to_tlog_set.Values) {
				// note: the preprocessor writes its output to the wrong path, then moves it
				tlog_set.outputs = tlog_set.outputs.Where(file => File.Exists(file)).ToList();
				// note: GetOutOfDateItems doesn't work when the source file is in the list
				// note: ignore temporary files like the RSP file for the command line
				// note: for some reason the the PDB is both read and written to
				// note: the inputs also must be unique
				tlog_set.inputs = tlog_set.inputs.Where(file => 
					file != tlog_set.src_file && File.Exists(file) && !tlog_set.outputs.Contains(file) 
				).ToHashSet().ToList();
			}
			
			to_delete.Add(prefixes_file);
		}
		
		ReadTLogs(src_to_tlog_set, directory);
		
		if(cppm_out_of_date) {
			for(int i = 0; i < 3; i++) {
				File.WriteAllLines(
					directory + "cppm." + rws[i] + ".1.tlog",
					src_to_tlog_set.Values.SelectMany(tlog_set => tlog_set.GetOutputLines(i)).ToArray()
				);
			}
		}
		
		foreach(string file in to_delete) {
			File.Delete(file);
		}
		
		/*foreach(TLogSet tlog_set in src_to_tlog_set.Values) {
			Log.LogMessage(MessageImportance.High, "{0}:", tlog_set.src_file);
			for(int i = 0; i < 3; i++) {
				Log.LogMessage(MessageImportance.High, "  {0}", rws[i]);
				foreach(string line in tlog_set.GetLines(i)) {
					Log.LogMessage(MessageImportance.High, "    {0}", line);
				}
			}
		}*/
		
		return src_to_tlog_set;
	}
	
	ITaskItem[] GetOOD(string tlog_directory, Dictionary<string, TLogSet> src_to_tlog_set)
	{
		var get_ood_sources = new List<TaskItem>();
		foreach(ITaskItem item in CL_Input_All) {
			var tlog_set = src_to_tlog_set[NormalizeFilePath(item.GetMetadata("FullPath"))];
			var ood_item = new TaskItem(tlog_set.src_file);
			ood_item.SetMetadata("write", String.Join(";", tlog_set.outputs));
			ood_item.SetMetadata("read", String.Join(";", tlog_set.inputs));
			// pass the current command instead of the previous command to GetOutOfDateItems
			//ood_item.SetMetadata("command", GetCommandFromItem(item));
			var dict = Get_CL_PropertyDictionary(item);
			AppendToDictionaryValue(dict, "AdditionalIncludeDirectories", 
				String.Join(";", legacy_header_paths), ";");
			string cmd = GetCommandFromPropertyDictionary(dict);
			/*if(tlog_set.command.Count > 0 && cmd != tlog_set.command[0]) {
				Log.LogMessage(MessageImportance.High, "'{0}' != '{1}'", cmd, tlog_set.command[0]);
			}*/
			ood_item.SetMetadata("command", cmd);
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
	
	bool ReadUpdateTLogs_GetOOD()
	{
		if(CL_Input_All.Length == 0)
			return true;
		
		// todo: try to merge the pp and src tracker logs to avoid checking twice
		// todo: merge all the tlogs from all the files into one tlog and check that first
		
		string tlog_dir = CL_Input_All[0].GetMetadata("TrackerLogDirectory");
		string tlog_dir_pp = tlog_dir + "pp\\";
		CL_Input_OutOfDate_PP = GetOOD(tlog_dir_pp, ReadUpdateTLogs(tlog_dir_pp));
		
		string tlog_dir_src = tlog_dir + "src\\";
		src_to_tlog_set_src = ReadUpdateTLogs(tlog_dir_src);
		CL_Input_OutOfDate_Src = GetOOD(tlog_dir_src, src_to_tlog_set_src);
		File.WriteAllLines(OutOfDate_File, CL_Input_OutOfDate_Src.Select(item => item.GetMetadata("FullPath")).ToArray());
		
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
	// changed BuildingInIDE to BuildingInIde, commented TrackFileAccess, MinimalRebuildFromTracking and AcceptableNonZeroExitCodes
	string[] props = { "BuildingInIde", "Sources", "AdditionalIncludeDirectories", "AdditionalOptions", "AdditionalUsingDirectories", "AssemblerListingLocation", "AssemblerOutput", "BasicRuntimeChecks", "BrowseInformation", "BrowseInformationFile", "BufferSecurityCheck", "CallingConvention", "ControlFlowGuard", "CompileAsManaged", "CompileAsWinRT", "CompileAs", "ConformanceMode", "DebugInformationFormat", "DiagnosticsFormat", "DisableLanguageExtensions", "DisableSpecificWarnings", "EnableEnhancedInstructionSet", "EnableFiberSafeOptimizations", "EnableModules", "EnableParallelCodeGeneration", "EnablePREfast", "EnforceTypeConversionRules", "ErrorReporting", "ExceptionHandling", "ExcludedInputPaths", "ExpandAttributedSource", "FavorSizeOrSpeed", "FloatingPointExceptions", "FloatingPointModel", "ForceConformanceInForLoopScope", "ForcedIncludeFiles", "ForcedUsingFiles", "FunctionLevelLinking", "GenerateXMLDocumentationFiles", "IgnoreStandardIncludePath", "InlineFunctionExpansion", "IntrinsicFunctions", "LanguageStandard", "MinimalRebuild", "MultiProcessorCompilation", "ObjectFileName", "OmitDefaultLibName", "OmitFramePointers", "OpenMPSupport", "Optimization", "PrecompiledHeader", "PrecompiledHeaderFile", "PrecompiledHeaderOutputFile", "PREfastAdditionalOptions", "PREfastAdditionalPlugins", "PREfastLog", "PreprocessKeepComments", "PreprocessorDefinitions", "PreprocessSuppressLineNumbers", "PreprocessToFile", "ProcessorNumber", "ProgramDataBaseFileName", "RemoveUnreferencedCodeData", "RuntimeLibrary", "RuntimeTypeInfo", "SDLCheck", "ShowIncludes", "WarningVersion", "SmallerTypeCheck", "SpectreMitigation", "StringPooling", "StructMemberAlignment", "SupportJustMyCode", "SuppressStartupBanner", "TreatSpecificWarningsAsErrors", "TreatWarningAsError", "TreatWChar_tAsBuiltInType", "UndefineAllPreprocessorDefinitions", "UndefinePreprocessorDefinitions", "UseFullPaths", "UseUnicodeForAssemblerListing", "WarningLevel", "WholeProgramOptimization", "WinRTNoStdLib", "XMLDocumentationFileName", "CreateHotpatchableImage", "TrackerLogDirectory", "TLogReadFiles", "TLogWriteFiles", "ToolExe", "ToolPath", /*"TrackFileAccess", "MinimalRebuildFromTracking",*/ "ToolArchitecture", "TrackerFrameworkPath", "TrackerSdkPath", "TrackedInputFilesToIgnore", "DeleteOutputOnExecute", /*"AcceptableNonZeroExitCodes",*/ "YieldDuringToolExecution" };
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
	
	CL GetCL_From_PropertyDictionary(Dictionary<string, string> dict) {
		CL cl = new CL {
			BuildEngine = this.BuildEngine,
		};
		foreach(KeyValuePair<string, string> entry in dict) {
			if(!SetPropertyFromString(cl, entry.Key, entry.Value)) return null;
		}
		return cl;
	}
	
	string GetCommandFrom(System.Func<string, string> prop_to_value_func) {
		return String.Join(";", props.Except(props_not_for_cmd).Select(prop => prop + "='" + prop_to_value_func(prop) + "'"));
	}
	
	string GetCommandFromItem(ITaskItem item) {
		return GetCommandFrom(prop => item.GetMetadata(prop));
	}
	
	string GetCommandFromPropertyDictionary(Dictionary<string, string> dict) {
		return GetCommandFrom(prop => {
			string ret = "";
			dict.TryGetValue(prop, out ret);
			return ret;
		});
	}
	
	String GetPreprocessedFile(ITaskItem item, Dictionary<string, string> property_dictionary, 
		string unique_prefix, string tracking_command, 
		bool is_legacy_header, IEnumerable<string> legacy_header_paths)
	{
		string src_file = item.GetMetadata("FullPath");
		CL cl = GetCL_From_PropertyDictionary(property_dictionary);
		if(cl == null)
			return null;
		if(is_legacy_header) {
			// create a wrapper that allows the legacy header to be imported along with its macros
			string src_file_name = Path.GetFileNameWithoutExtension(src_file);
			string additional_options = "/module:name " + src_file_name.ToUpper() +
				" /module:exportActiveMacros /module:wrapper \"" + BMI_Path + src_file_name + ".lh\" ";
			cl.AdditionalOptions = additional_options + cl.AdditionalOptions;
		} else {
			cl.PreprocessToFile = true;
		}
		StartTracking(cl.TrackerLogDirectory + "pp\\", unique_prefix);
		bool success = cl.Execute();
		EndTracking(success, cl.TrackerLogDirectory + "pp\\", unique_prefix, src_file, tracking_command);
		if(!success)
			return null;
		// todo: if the file depends on a legacy header, it only reads the .lh (or, eventually the bmi of the header)
		// so we need to add the header to its list of dependencies
		if(is_legacy_header)
			return "";
		string pre_file = item.GetMetadata("ObjectFileName").ToString() + item.GetMetadata("Filename").ToString() + ".i";
		return File.ReadAllText(pre_file);
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
		public bool legacy_header = false;
		public List<string> imported_modules = new List<string>();
		public List<string> legacy_imports = new List<string>();
		public List<string> included_headers = new List<string>();
		public Dictionary<string, string> cl_params = new Dictionary<string, string>();
		
		public void assign(ModuleDefinition def) {
			bmi_file = def.bmi_file;
			exported_module = def.exported_module;
			legacy_header = def.legacy_header;
			imported_modules = def.imported_modules;
			legacy_imports = def.legacy_imports;
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
	
	//JavaScriptSerializer serializer = new JavaScriptSerializer();
	
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
	
	delegate bool PreProcessFunc(ITaskItem item);
	
	void AppendToDictionaryValue(Dictionary<string, string> dict, string key, string value_to_append, string separator) {
		string value = null;
		if(dict.TryGetValue(key, out value)) {
			if(value != "") value += separator;
			value += value_to_append;
			dict[key] = value;
		} else {
			dict.Add(key, value_to_append);
		}
	}

	bool PreProcess()
	{
		Log.LogMessage(MessageImportance.High, "CppM: PreProcess");
		
		if(CL_Input_OutOfDate_PP.Length == 0) {
			File.SetLastWriteTimeUtc(ModuleMapFile, System.DateTime.UtcNow);
			return true;
		}
		
		//todo:
		//Regex raw_string_pattern = new Regex("R\"([^(]*)\\([^\"]*\\)\g1\"");
		Regex string_pattern = new Regex("\"[^\"]*\"");
		Regex export_pattern = new Regex(@"(?:^|[\s;])export module ([\w.]+);");
		Regex import_pattern = new Regex(@"(?:^|[\s;])import ([\w.]+);");
		
		var old_module_map = DeserializeIfExists<ModuleMap>(ModuleMapFile);
		var old_src_to_entry = GetModuleMap_SrcToEntry(old_module_map);
		
		ModuleMap module_map = new ModuleMap();
		
		var ood_files = CL_Input_OutOfDate_PP.Select(item => 
			NormalizeFilePath(item.GetMetadata("FullPath"))).ToHashSet();
		
		int prefix_id = 0;
		
		PreProcessFunc pp_func = (item) => {
			string src_file = NormalizeFilePath(item.GetMetadata("Sources"));
			string def_file = item.GetMetadata("CppM_ModuleDefinitionFile").ToString();
			
			ModuleMap.Entry entry_to_add = null;
			if(!ood_files.Contains(src_file)) {
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
			} else {
				var module_def = new ModuleDefinition {
					cl_params = Get_CL_PropertyDictionary(item),
				};
				
				module_def.legacy_header = (item.GetMetadata("CppM_Legacy_Header") == "true");
				// todo: there should be a modules specific option to pass the set of legacy headers
				AppendToDictionaryValue(module_def.cl_params, "AdditionalIncludeDirectories", 
					String.Join(";", legacy_header_paths), ";");
				
				string tracking_command = GetCommandFromPropertyDictionary(module_def.cl_params);
				prefix_id++;
				String preprocessed = GetPreprocessedFile(item, module_def.cl_params,
					prefix_id.ToString(), tracking_command, 
					module_def.legacy_header, legacy_header_paths);
				if(preprocessed == null)
					return false;
				
				//preprocessed = raw_string_pattern.Replace(preprocessed, "");
				preprocessed = string_pattern.Replace(preprocessed, "");
				
				Match m = null;
				if(module_def.legacy_header) {
					module_def.exported_module = Path.GetFileNameWithoutExtension(src_file);
				} else {
					m = export_pattern.Match(preprocessed);
					if(m.Success) {
						module_def.exported_module = m.Groups[1].ToString();
					}
				}
				
				if(module_def.exported_module != "") {
					module_def.bmi_file = BMI_Path + module_def.exported_module + BMI_Ext;
				}
				
				m = import_pattern.Match(preprocessed);
				while(m.Success) {
					String module = m.Groups[1].ToString();
					m = m.NextMatch();
					if(!module.StartsWith("std."))
						module_def.imported_modules.Add(module);
				}
				
				SerializeToFile(module_def, def_file);
				
				entry_to_add = ModuleMap.Entry.create(src_file, module_def);
			}
			module_map.entries.Add(entry_to_add);
			return true;
		};
		
		// the legacy headers might export macros that affect the rest of the preprocessing
		foreach(ITaskItem item in CL_Input_All)
			if(item.GetMetadata("CppM_Legacy_Header") == "true" && !pp_func(item))
				return false;
		
		foreach(ITaskItem item in CL_Input_All)
			if(item.GetMetadata("CppM_Legacy_Header") != "true" && !pp_func(item))
				return false;
		
		SerializeToFile(module_map, ModuleMapFile);
		//Log.LogMessage(MessageImportance.High, "test: {0}", serializer.Serialize(module_map));
		
		return true;
	}
	
	public class GlobalModuleMap
	{
		public ModuleMap map = null;
		public string ood_file = "";
		public LinkedList<string> ood_file_list = null;
		public bool ood_list_changed = false;
	}
	
	public class GlobalModuleMapNode
	{
		public GlobalModuleMap map = null;
		public ModuleMap.Entry entry = null;
		
		public bool visited = false;
		public bool is_selected = false;
		// selected source files that are not up to date with respect to their object file:
		public bool need_obj = false;
		// module interface units in the current project transitively imported by the above
		// that are not up to date with respect to their bmi file:
		public bool need_bmi = false;
		public bool subtree_has_leaves = false;
		public List<GlobalModuleMapNode> imports_nodes = new List<GlobalModuleMapNode>();
		public List<GlobalModuleMapNode> imported_by_nodes = new List<GlobalModuleMapNode>();
		public int remaining_imports_to_check = 0;
		public bool imports_changed = false;
		public bool build_finished = false;
		public LinkedListNode<string> ood_list_node = null;
	}
	
	Dictionary<string, GlobalModuleMapNode> global_module_to_node = new Dictionary<string, GlobalModuleMapNode>();
	Dictionary<string, GlobalModuleMapNode> local_source_to_node = new Dictionary<string, GlobalModuleMapNode>();
	List<GlobalModuleMap> all_global_module_maps = new List<GlobalModuleMap>();
	Dictionary<string, TLogSet> src_to_tlog_set_src = new Dictionary<string, TLogSet>();
	
	bool GetGlobalModuleMap()
	{
		// unless some specific files/projects are manually selected (to build only that)
		// the references of this project are assumed to have been built already
		// (either by the IDE, or by passing BuildProjectReferences=true to MSBuild)
		if(CL_Input_Manually_Selected.Length > 0 && ProjectReference.Length > 0) {
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
			if(!build.Execute())
				return false;
		}
		
		var project_source_to_node = new Dictionary<string, GlobalModuleMapNode>();
		
		// todo: try to keep the while project tree and the global module map in memory 
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
			
			if(project_file != CurrentProject) {
				// todo: maybe don't load this until it's definitely needed ? also, load only outputs
				InitTLogs(src_to_tlog_set_src, module_map.entries.Select(entry => entry.source_file));
				ReadTLogs(src_to_tlog_set_src, project.GetPropertyValue("CppM_TLogLocation_FullPath") + "src\\");
			}
			
			string ood_file = project.GetPropertyValue("CppM_OutOfDate_File");
				
			var global_map = new GlobalModuleMap{ map = module_map, ood_file = ood_file };
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
			
			if(File.Exists(ood_file)) {
				var list = new LinkedList<string>(File.ReadAllLines(ood_file));
				global_map.ood_file_list = list;
				for(var list_node = list.First; list_node != null; list_node = list_node.Next) {
					source_to_node[list_node.Value].ood_list_node = list_node;
				}
			}
		}
		
		return true;
	}
	
	byte[] GetBMI_Hash(string bmi_file) {
		try {
			byte[] bytes = File.ReadAllBytes(bmi_file);
			if(bytes.Length < (4+32))
				throw new System.Exception("invalid bmi file structure for " + bmi_file);
			return bytes.Skip(4).Take(32).ToArray();
		} catch(System.IO.FileNotFoundException) {
			return null;
		}
	}
	
	class ModuleNotFoundException : System.Exception {
		
	}
	
	bool QueueLeavesToBuild(GlobalModuleMapNode node, Queue<GlobalModuleMapNode> build_queue)
	{
		if(node.visited)
			return node.subtree_has_leaves;
		node.visited = true;
		
		string src_file = node.entry.source_file;
		if(node.ood_list_node != null) {
			if(node.is_selected) {
				//Log.LogMessage(MessageImportance.High,"need object file for {0}", src_file);
				node.need_obj = true;
			} else {
				//Log.LogMessage(MessageImportance.High,"need bmi file for {0}", src_file);
				node.need_bmi = true;
			}
		}
		
		bool has_leaf_descendants = false;
		foreach(string import in node.entry.imported_modules) {
			GlobalModuleMapNode import_node = null;
			if(!global_module_to_node.TryGetValue(import, out import_node)) {
				Log.LogMessage(MessageImportance.High, "imported module '{0}' not found in global module map", import);
				throw new ModuleNotFoundException();
			}
			// downward edges are needed in the whole subtree for the references generation
			node.imports_nodes.Add(import_node);
			
			if(QueueLeavesToBuild(import_node, build_queue)) {
				has_leaf_descendants = true;
				// updward edges should only go up from leaves for the final build queue to work
				import_node.imported_by_nodes.Add(node);
				node.remaining_imports_to_check++;
			}
		}
		
		bool is_leaf = (node.need_obj || node.need_bmi) && !has_leaf_descendants;
		if(is_leaf) {
			//Log.LogMessage(MessageImportance.High, "found leaf {0}", node.entry.source_file);
			node.imports_changed = true;
			build_queue.Enqueue(node);
		}
		
		node.subtree_has_leaves = has_leaf_descendants || is_leaf;
		return node.subtree_has_leaves;
	}
	
	// todo: use visitor colors ?
	void ClearVisited(GlobalModuleMapNode node) {
		if(!node.visited)
			return;
		node.visited = false;
		foreach(GlobalModuleMapNode imported_node in node.imports_nodes)
			ClearVisited(imported_node);
	}
	
	// todo: should this be cached per node ?
	string GetModuleReferences(GlobalModuleMapNode node) {
		if(node.visited)
			return "";
		node.visited = true;
		
		string ret = node.entry.bmi_file != "" ? "/module:reference \"" + node.entry.bmi_file + "\" " : "";
		foreach(GlobalModuleMapNode imported_node in node.imports_nodes) {
			ret += GetModuleReferences(imported_node);
		}
		return ret;
	}
	
	bool Compile(GlobalModuleMapNode node, int prefix_id)
	{
		var cl = GetCL_From_PropertyDictionary(node.entry.cl_params);
		
		byte[] old_hash = null;
		if(node.entry.exported_module != "") {
			old_hash = GetBMI_Hash(node.entry.bmi_file);
		}
		
		if(node.need_bmi) {
			//todo: only get the bmi, don't do a full compile ?
			//or maybe get the bmi, queue nodes that depend on it, then continue with the full compile ?
		}
		
		//Log.LogMessage(MessageImportance.High, "building {0}", cl.Sources[0]);
		
		ClearVisited(node);
		string additional_options = GetModuleReferences(node);
		if(node.entry.exported_module != "") {
			if(node.entry.legacy_header) {
				additional_options += "/module:export /module:name " + node.entry.exported_module + " ";
			} else {
				additional_options += "/module:interface ";
			}
			additional_options += "/module:output \"" + node.entry.bmi_file + "\" ";
		}
		// AdditionalOptions already contains /modules:stdifcdir
		cl.AdditionalOptions = additional_options + cl.AdditionalOptions;
		//note: we assume modules are already enabled and the standard is set to latest
		//Log.LogMessage(MessageImportance.High, "additional options for '{0}' are: {1}", node.entry.source_file, additional_options);

		// we don't record the added options in the tracked command because
		// we need to compare it later to the options before preprocessing
		string tracking_command = GetCommandFromPropertyDictionary(node.entry.cl_params);
		string prefix = prefix_id.ToString();
		StartTracking(cl.TrackerLogDirectory + "src\\", prefix);
		bool success = cl.Execute();
		EndTracking(success, cl.TrackerLogDirectory + "src\\", prefix, node.entry.source_file, tracking_command);
		if(!success)
			return false;
		
		if(node.entry.exported_module != "") {
			byte[] new_hash = GetBMI_Hash(node.entry.bmi_file);
			if(new_hash == null) {
				Log.LogMessage(MessageImportance.High, "ERROR: bmi file {0} not generated for source file {1}",
					node.entry.bmi_file, node.entry.source_file);
				return false;
			}
			bool bmi_changed = old_hash == null || !old_hash.SequenceEqual(new_hash);
			
			if(bmi_changed) {
				// assume imported_by only contains edges in the right subgraph
				foreach(GlobalModuleMapNode imported_by in node.imported_by_nodes) {
					imported_by.imports_changed = true;
				}
			} else {
				Log.LogMessage(MessageImportance.High, "bmi did not change for {0}", node.entry.source_file);
			}
		}
		
		if(node.ood_list_node != null) {
			node.map.ood_file_list.Remove(node.ood_list_node);
			node.map.ood_list_changed = true;
			node.ood_list_node = null;
		}

		return true;
	}
	
	bool TouchOutputs(GlobalModuleMapNode node)
	{
		Log.LogMessage(MessageImportance.High, "touching output files for {0}", node.entry.source_file);
		foreach(string output_file in src_to_tlog_set_src[node.entry.source_file].outputs) {
			File.SetLastWriteTimeUtc(output_file, System.DateTime.UtcNow);
		}
		return true;
	}
	
	bool Compile()
	{
		Log.LogMessage(MessageImportance.High, "CppM: Compile");
		
		if(!GetGlobalModuleMap()) {
			Log.LogMessage(MessageImportance.High, "failed to read global module map");
			return false;
		}
		
		var selected_nodes = new List<GlobalModuleMapNode>();
		
		foreach(ITaskItem item in CL_Input_Selected) {
			string src_file = NormalizeFilePath(item.GetMetadata("Sources"));
			GlobalModuleMapNode node = null;
			if(!local_source_to_node.TryGetValue(src_file, out node)) {
				Log.LogMessage(MessageImportance.High, "ERROR: src_file {0} not found in local map", src_file);
				return false;
			}
			node.is_selected = true;
			selected_nodes.Add(node);
		}

		var build_queue = new Queue<GlobalModuleMapNode>();

		try {
			foreach(GlobalModuleMapNode node in selected_nodes) {
				QueueLeavesToBuild(node, build_queue);
			}
		} catch(ModuleNotFoundException) {
			return false;
		}

		int prefix_id = 1;
		
		// todo: use a concurrent depth-based priority queue to parallelize this
		
		while(build_queue.Count > 0) {
			var node = build_queue.Dequeue();
			if(node.imports_changed || node.need_bmi || node.need_obj) {
				if(!Compile(node, prefix_id++))
					return false;
			} else {
				if(!TouchOutputs(node))
					return false;
			}
			node.build_finished = true; // just for the sanity check
			
			foreach(GlobalModuleMapNode imported_by in node.imported_by_nodes) {
				imported_by.remaining_imports_to_check--;
				if(imported_by.remaining_imports_to_check == 0) {
					build_queue.Enqueue(imported_by);
				}
			}
		}
		
		bool ok = true;
		foreach(GlobalModuleMapNode node in selected_nodes) {
			if(node.need_obj && !node.build_finished) {
				Log.LogMessage(MessageImportance.High, "ERROR: selected file {0} was not built", node.entry.source_file);
				ok = false;
			}
		}
		
		foreach(GlobalModuleMap map in all_global_module_maps) {
			if(map.ood_list_changed) {
				File.WriteAllLines(map.ood_file, map.ood_file_list.ToArray());
			}
		}
		
		return ok;
	}

	public override bool Execute()
	{
		return CreateProjectCollection() && 
			GetLegacyHeaderPaths() &&
			ReadUpdateTLogs_GetOOD() && 
			PreProcess() &&
			(PreProcess_Only == "true" || Compile());
	}
}