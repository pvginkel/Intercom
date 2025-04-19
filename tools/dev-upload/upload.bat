@echo off

setlocal enableextensions enabledelayedexpansion

pushd "%~dp0"

cd ..\..

docker build ^
    --tag intercom-uploader ^
    . ^
    --file tools/dev-upload/Dockerfile 

echo "%~dp0"

docker run ^
    --rm ^
    -v %CD%/build/intercom.bin:/workspace/app/intercom-ota.bin ^
    -v %CD%/../HelmCharts/assets:/workspace/keys ^
    --add-host iotsupport.home:192.168.178.62 ^
    intercom-uploader ^
    /workspace/keys/kubernetes-signing-key ^
    /workspace/app/intercom-ota.bin ^
    iotsupport.home

popd
