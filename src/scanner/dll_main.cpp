// from https://stackoverflow.com/questions/1373100/how-to-add-folder-to-assembly-search-path-at-runtime-in-net
using namespace System;
using namespace System::IO;
using namespace System::Reflection;

static Assembly^ LoadFromSameFolder(Object^ sender, ResolveEventArgs^ args)
{
	String^ folderPath = Path::GetDirectoryName(Assembly::GetExecutingAssembly()->Location);
	String^ assemblyPath = Path::Combine(folderPath, (gcnew AssemblyName(args->Name))->Name + ".dll");
	if (File::Exists(assemblyPath) == false) return nullptr;
	Assembly^ assembly = Assembly::LoadFrom(assemblyPath);
	return assembly;
}

struct __declspec(dllexport) ScannerMain {
	ScannerMain() {
		System::AppDomain^ currentDomain = AppDomain::CurrentDomain;
		currentDomain->AssemblyResolve += gcnew ResolveEventHandler(LoadFromSameFolder);
	}
};

#pragma unmanaged
// Global instance of object
ScannerMain scannerMain;