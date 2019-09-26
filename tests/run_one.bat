@ECHO OFF
set test=%1
set verbosity=%2

set current_path=%cd%
set mypath=%~dp0

if "%test%" == "" goto fail
if not exist "%mypath%\%test%" goto fail

if "%verbosity%" == "" set verbosity=m
set verbosity_long=minimal
if "%verbosity%" == "d" set verbosity_long=diagnostic

cd %mypath%\%test%
if not exist build mkdir build
cd build

cmake -G "Visual Studio 16 2019" -A "X64" ../
set solution_dir=%mypath%\%test%\build\
cmake --build . --config Debug --parallel -- -v:%verbosity% "/p:Xt_BuildLogPath=%solution_dir%build.log;SolutionDir=%solution_dir%;SolutionPath=%solution_dir%test_%test%.sln" -flp:LogFile=build.log;Verbosity=%verbosity_long%

:end
cd %current_path%
exit /B 0

:fail
cd %current_path%
exit /B 1