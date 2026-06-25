PROJECT_NAME=`grep -Eo 'project\(.*\)' CMakeLists.txt | cut -d '(' -f 2 | cut -d ')' -f 1`
cmake .
make
zip -u $PROJECT_NAME.vpk -r html/*
zip -u $PROJECT_NAME.vpk -j html/quark.js html/Sans.ttf html/style.css
zip -u $PROJECT_NAME.vpk -r payloads/* #usbserial_proxy.skprx

