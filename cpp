(base) PS C:\Users\YuhengLiu\Desktop\ami_client_cpp> 

cmake -B build -S . -DCMAKE_TOOLCHAIN_FILE=C:/Users/YuhengLiu/vcpkg/scripts/buildsystems/vcpkg.cmake                           
cmake --build build     
.\build\Debug\main.exe