@ECHO OFF
set current_path=%cd%
set mypath=%~dp0

cd %mypath%

for %%T in (dag concurrent) do (
	call full_clean_one.bat %%T
	call run_one.bat %%T
	if not exist %mypath%\%%T\build\Debug\A.exe (
		echo %%T test failed
		goto fail
	)
	call run_one.bat %%T
)
echo all tests successful

:end
cd %current_path%
exit /B 0

:fail
cd %current_path%
exit /B 1