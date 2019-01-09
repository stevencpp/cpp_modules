@ECHO OFF
set current_path=%cd%
set mypath=%~dp0

for %%T in (dag) do (
	call full_clean_one.bat %%T
	call run_one.bat %%T
	if not exist %mypath%\%%T\build\Debug\A.exe (
		echo %%T test failed
		goto end
	)
)
echo all tests successful

:end