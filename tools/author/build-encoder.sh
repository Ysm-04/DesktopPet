#!/usr/bin/env bash
# Run from an ASCII build directory containing the verified source archives.
# Developer dependencies: Git Bash, MinGW-w64 GCC 11+, CMake, Ninja, GNU make.
# Runtime users need none of these. No network access during this build.
set -euo pipefail
root="$(pwd)"
prefix="$root/install"
winprefix="$(cygpath -m "$prefix")"
mkdir -p "$prefix" "$root/dev-bin"
export PATH="$root/dev-bin:$PATH"
export SHELL="D:/Git/bin/bash.exe"
export CC=gcc CXX=g++ AR=ar RANLIB=ranlib
# Optional isolated compiler-helper directory, needed by MSIX child processes
# when the original cc1 cannot find libwinpthread through PATH.
if test -d "$root/compiler"; then
    helper="-B$(cygpath -m "$root/compiler")/"
    export CFLAGS="$helper" CXXFLAGS="$helper" LDFLAGS="$helper"
    export CC="gcc $helper" CXX="g++ $helper"
fi
for archive in sources/*.tar.gz; do
    [[ "$archive" == *ffmpeg-7.1.1* ]] && continue
    case "$archive" in
        *ffmpeg*) expected=FFmpeg-n7.1.5 ;;
        *libwebp*) expected=libwebp-1.6.0 ;;
        *libvpx*) expected=libvpx-1.15.0 ;;
        *x264*) expected=x264-31e19f92f00c7003fa115047ce50978bc98c3a0d ;;
        *zlib*) expected=zlib-1.3.1 ;;
        *) echo "Unexpected source archive: $archive" >&2; exit 1 ;;
    esac
    test -d "$expected" || tar -xzf "$archive"
done

cmake -S zlib-1.3.1 -B zlib-build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_C_FLAGS="${CFLAGS:-}" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$winprefix" -DZLIB_BUILD_EXAMPLES=OFF
cmake --build zlib-build --target zlibstatic --parallel 4
mkdir -p "$prefix/lib" "$prefix/include"
cp zlib-build/libzlibstatic.a "$prefix/lib/libz.a"
cp zlib-1.3.1/zlib.h zlib-build/zconf.h "$prefix/include/"
cmake -S libwebp-1.6.0 -B webp-build -G Ninja -DCMAKE_C_COMPILER=gcc -DCMAKE_CXX_COMPILER=g++ \
    -DCMAKE_C_FLAGS="${CFLAGS:-}" -DCMAKE_CXX_FLAGS="${CXXFLAGS:-}" \
    -DCMAKE_BUILD_TYPE=Release -DCMAKE_INSTALL_PREFIX="$winprefix" -DBUILD_SHARED_LIBS=OFF \
    -DWEBP_BUILD_ANIM_UTILS=OFF -DWEBP_BUILD_CWEBP=OFF -DWEBP_BUILD_DWEBP=OFF \
    -DWEBP_BUILD_GIF2WEBP=OFF -DWEBP_BUILD_IMG2WEBP=OFF -DWEBP_BUILD_VWEBP=OFF \
    -DWEBP_BUILD_WEBPINFO=OFF -DWEBP_BUILD_EXTRAS=OFF
cmake --build webp-build --parallel 4
cmake --install webp-build
(
    cd x264-31e19f92f00c7003fa115047ce50978bc98c3a0d
    if ! test -f config.mak; then
        ./configure --host=x86_64-w64-mingw32 --prefix="$winprefix" --enable-static \
            --disable-cli --disable-asm --disable-opencl
    fi
    # x264 emits its entire dependency scan as one command, exceeding Windows'
    # command-line limit. A fresh source build needs no incremental dependency
    # scan; generated config headers still retain their normal prerequisites.
    make SHELL="$SHELL" -o .depend -j4
    make SHELL="$SHELL" -o .depend install-lib-static install-lib-dev
)
(
    cd libvpx-1.15.0
    if ! test -f config.mk; then
        ./configure --target=generic-gnu --prefix="$winprefix" --disable-examples --disable-tools \
            --disable-docs --disable-unit-tests --disable-vp8 --disable-vp9-encoder
    fi
    # Native MinGW make does not understand MSYS /d/... prerequisites.
    sed -i "s|$root|$(cygpath -m "$root")|g" libs-generic-gnu.mk
    make SHELL="$SHELL" -j4
    make SHELL="$SHELL" install
)

# Small, deterministic pkg-config adapter for exactly these statically linked
# dependencies. It intentionally cannot discover unrelated machine packages.
cat > dev-bin/pet-pkg-config <<EOF
#!/usr/bin/env bash
set -e
case "\$*" in
  *--version*) echo 1.0; exit 0 ;;
  *--exists*) exit 0 ;;
esac
result=""
case "\$*" in
  *--cflags*) result="-I$winprefix/include" ;;
esac
if [[ "\$*" == *--libs* ]]; then
    result="\$result -L$winprefix/lib"
    case "\$*" in
      *x264*) result="\$result -lx264" ;;
      *webpmux*) result="\$result -lwebpmux -lwebp -lsharpyuv -lm" ;;
      *webp*) result="\$result -lwebp -lsharpyuv -lm" ;;
      *vpx*) result="\$result -lvpx -lm" ;;
      *zlib*) result="\$result -lz" ;;
      *) exit 1 ;;
    esac
fi
echo "\$result"
EOF
chmod +x dev-bin/pet-pkg-config
cat > dev-bin/pet-windres <<EOF
#!/usr/bin/env bash
exec windres --preprocessor="$CC -E -xc -DRC_INVOKED" "\$@"
EOF
chmod +x dev-bin/pet-windres
(
    cd FFmpeg-n7.1.5
    # n7.1.5's minimal H.264-only selection calls the shared AOM cleanup
    # without listing its object. Keep this reproducible one-line build fix
    # here; original source archives remain unmodified in the distribution.
    sed -i '/^OBJS-$(CONFIG_H264_SEI)/s/h2645_sei.o$/h2645_sei.o aom_film_grain.o/' libavcodec/Makefile
    if ! test -f ffbuild/config.mak; then
        ./configure --prefix="$winprefix" --target-os=mingw32 --arch=x86_64 --cc="$CC" --cxx="$CXX" \
            --pkg-config="$root/dev-bin/pet-pkg-config" --pkg-config-flags=--static \
            --extra-cflags="-I$winprefix/include" --extra-ldflags="-L$winprefix/lib -static" \
            --disable-autodetect --disable-network --disable-everything --disable-asm \
            --disable-doc --disable-debug --disable-shared --enable-static \
            --enable-ffmpeg --enable-ffprobe --disable-ffplay --enable-gpl \
            --enable-libx264 --enable-libwebp --enable-libvpx --enable-zlib \
            --enable-avcodec --enable-avformat --enable-avfilter --enable-swscale --enable-swresample \
            --enable-protocol=file,pipe \
            --enable-demuxer=mov,matroska,image2,concat \
            --enable-muxer=mp4,webp,image2,null,matroska,mov \
            --enable-decoder=h264,prores,libvpx_vp9,png,rawvideo \
            --enable-encoder=libx264,libwebp_anim,libwebp,png,rawvideo,wrapped_avframe,prores_ks,libvpx_vp9 \
            --enable-parser=h264,vp9,png \
            --enable-filter=buffer,buffersink,format,scale,pad,fps,split,alphaextract,alphamerge,hstack,vstack,crop,colorkey,despill,null \
            --enable-bsf=h264_mp4toannexb
    fi
    make SHELL="$SHELL" WINDRES="bash $root/dev-bin/pet-windres" -j4
    make SHELL="$SHELL" WINDRES="bash $root/dev-bin/pet-windres" install
)
mkdir -p bundle
cp "$prefix/bin/ffmpeg.exe" "$prefix/bin/ffprobe.exe" bundle/
cp -r sources bundle/
cp "$0" bundle/build-encoder.sh
cp dev-bin/pet-pkg-config dev-bin/pet-windres bundle/
cp FFmpeg-n7.1.5/ffbuild/config.mak bundle/ffmpeg-config.mak
cp FFmpeg-n7.1.5/config.h bundle/ffmpeg-config.h
mkdir -p bundle/licenses
cp FFmpeg-n7.1.5/COPYING* bundle/licenses/
cp x264-31e19f92f00c7003fa115047ce50978bc98c3a0d/COPYING bundle/licenses/x264-COPYING
cp libwebp-1.6.0/COPYING bundle/licenses/libwebp-COPYING
cp libwebp-1.6.0/PATENTS bundle/licenses/libwebp-PATENTS
cp libvpx-1.15.0/LICENSE bundle/licenses/libvpx-LICENSE
cp libvpx-1.15.0/PATENTS bundle/licenses/libvpx-PATENTS
cp zlib-1.3.1/LICENSE bundle/licenses/zlib-LICENSE
cp sources/sources.json bundle/sources.json
bundle/ffmpeg.exe -version > bundle/version.txt
bundle/ffmpeg.exe -buildconf > bundle/buildconf.txt 2>&1
