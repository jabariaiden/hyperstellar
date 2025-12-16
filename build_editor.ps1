# Builds stellar_main.exe (editor)
Remove-Item build -Recurse -Force -ErrorAction SilentlyContinue
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
Copy-Item "build\Release\stellar_main.exe" "editor.exe" -Force