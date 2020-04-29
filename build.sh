# export NDK_HOME="/mnt/disk1/build_tool/android-ndk-r9d"

list=` find . -name "Android.mk" `
for alpha in $list;do
    currDir=$(cd "$(dirname $alpha)"; pwd)  
    ndk-build NDK_PROJECT_PATH=$currDir APP_BUILD_SCRIPT=$currDir/Android.mk APP_ALLOW_MISSING_DEPS=true clean
    ndk-build NDK_PROJECT_PATH=$currDir APP_BUILD_SCRIPT=$currDir/Android.mk NDK_APPLICATION_MK=$currDir/Application.mk V=1 -B 
    #APP_ALLOW_MISSING_DEPS=true APP_PLATFORM=android-9 APP_ABI=armeabi-v7a NDK_TOOLCHAIN_VERSION=4.8 APP_CFLAGS=-D__STDC_FORMAT_MACROS APP_STL=gnustl_static V=1
done
outDir=./out
rm -rf $outDir
mkdir $outDir
#rm -rf ` find $outDir -name "*webrtc*" `
cp -r ` find . -name "obj" ` $outDir
