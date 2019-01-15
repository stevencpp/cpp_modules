# cpp_modules

### 1. What is this ?

As a proof of concept, this build customization provides partial C++ modules support for MSBuild. Module export/import works. The module map is extracted as a preprocessing step and then the modules are built in order without having to manually specify module search paths. Legacy headers can be designated and imported with some limitations. Compiling a single file builds the transitively imported files in referenced project first if any are out of date. If the hash of a BMI of a given module does not change during compilation, other files that import it are not rebuilt unless they're otherwise out of date. Existing MSBuild projects can import the provided .targets and .props files, and applications that use CMake to generate the MSBuild projects can just add two lines to CMakeLists.txt (see tests/dag as an example).

### 2. How does this work ?

To properly and efficiently support legacy header units, module implementation files, module fragments etc. a dedicated module specific preprocessor will be needed. For now the module map extraction is done by asking the compiler to preprocess the file, and then running some regular expressions on the result to remove string literals and find the exports and imports.
There doesn't appear to be a good way to find legacy headers automatically, so they need to be marked as a legacy header explicitly in the project configuration, and then the set of legacy headers for a project should be the union of its legacy headers and those of any transitively referenced project. Legacy headers can import modules and/or other legacy headers, so the BMI cannot be generated before preprocessing and compiling those imported modules/headers. But those cannot be preprocessed until we know the macros exported from the legacy headers, so the macros need to be extracted first, without generating a BMI yet. Currently MSVC can generate a wrapper for a legacy header which contains an `import module_name;` followed by the macros in the original header. So this is used before preprocessing other sources, which #include the generated wrapper, to simulate standard legacy headers. The paths to where the wrapper headers are generated for all referenced projects are added to the include search path for both preprocessing, compilation and out of date checking. But this currently seems to require compiling the legacy header as well so it requires the imports to be available. Without a better a preprocessor, for now we assume that the legacy headers don't contain any non-standard imports. After all the preprocessing, the BMI is extracted from the legacy headers according to the build DAG.
As the CL task's built-in file access tracker did not appear to work, the FileTracker is invoked manually to extract each inputs/outputs of a given compilation (both preprocessing and final), and then the existing GetOutOfDateItems task is used to find which files need to be preprocessed and/or compiled. The set of out of date source files for a given project is cached to a file, so that projects which reference it don't need to recompute that set if they need to compile source files from the referenced project.
A module map file is generated for each project that contains the exported/imported modules as well as the full set of compile options for each file (to allow cross-project compilation). Before compiling, a "global" module map is constructed in memory by aggregating the set of module maps for the current and transitively referenced projects. Any duplicate or imported but missing modules abort the build. Ideally this global module map should be kept in memory during the entire build, but currently it gets recreated for every project when building the whole project tree.
When files are manually selected to be compiled (instead of building the project tree), referenced projects are not built by default, so to ensure that the global module map is up to date, the preprocessing step (but not compilation) for all transitively referenced projects is triggered explicitly.
A depth first search based algorithm adds the out-of-date leaf modules to a build queue which then builds additional modules as soon as their dependencies have been satisfied. Currently the build queue is serial, but could be replaced with e.g a concurrent depth-based priority queue.
The .ifc BMI files already contain the SHA256 hash of the rest of their content, so just that 32 byte hash can simply and efficiently be read before and after the compilation to see if the BMI changed. If it did not and hence some other files that import it don't need to be rebuilt, to preserve the invariants of the build system which is otherwise timestamp based, their modification timestamp is updated. This optimization doesn't work yet across project boundaries, but that could be fixed by storing the hashes for all input BMIs of any given module.
The BMI paths for all of the referenced modules are passed to the compiler, so in principle it shouldn't need to search directories for any modules.
The build customization distinguishes between source files for which an object file needs to be built and those where only building a BMI might be sufficient, but it would need compiler support to only compile the BMI. It would also be useful if the compiler offered a way to compile both, but write out the BMI first, as fast as possible, before going on to create the object file, so then you could start building other sources which import the BMI much sooner and improve scalability. Unless specific sets of files are compiled, MSBuild and the VS IDE builds a project's references first, which is also not ideal for scalability because some modules could be compiled in parallel with other modules from a referenced project if not imported by the former.

### 3. How to build and install it ?

- Clone this repo and use CMake to generate Visual Studio solutions for it.
- Build it. The tool will automatically be installed to `C:/Program Files/cpp_modules` after the build finishes. 
