AC_DEFUN([XINE_LIB_SUMMARY], [
    dnl ---------------------------------------------
    dnl Some infos:
    dnl ---------------------------------------------

    echo "xine-lib summary:"
    echo "----------------"

    dnl Input
    echo " * input plugins:"
    echo "   - file          - net"
    echo "   - stdin_fifo    - rtp"
    echo "   - http          - mms"
    echo "   - pnm           - rtsp"
    if test x"$enable_vcd" != x"no"; then
        if test x"$enable_vcdo" != x"no"; then
            echo "   - vcdo          - vcd"
        else
            echo "   - vcd"
        fi
    fi
    if test x"$with_external_dvdnav" != x"no"; then
        echo "   - dvd (external libs)"
    else
        echo "   - dvd (*INTERNAL* libs)"
    fi
    test x"$enable_vdr" != x"no"    && echo "   - vdr"
    test x"$have_dvb" = x"yes"      && echo "   - dvb"
    test x"$have_gnomevfs" = x"yes" && echo "   - gnome-vfs"
    test x"$have_samba" = x"yes"    && echo "   - smb"
    test x"$have_v4l" = x"yes"      && echo "   - v4l"
    test x"$have_v4l2" = x"yes"     && echo "   - v4l2"
    echo "   - cdda"
    test x"$have_libbluray" = x"yes" && echo "   - bluray"
    test x"$have_avformat" = x"yes"  && echo "   - avio (libavformat)"
    echo "   - test"
    echo ""

    dnl Demuxers
    echo " * demultiplexer plugins:"
    echo "   - avi           - mpeg"
    echo "   - mpeg_block    - mpeg_audio"
    echo "   - mpeg_elem     - mpeg_pes"
    echo "   - mpeg_ts       - qt/mpeg-4"
    echo "   - film          - roq"
    echo "   - fli           - smjpeg"
    echo "   - idcin         - wav"
    echo "   - wc3 mve       - voc"
    echo "   - vqa           - aiff"
    echo "   - cdda          - snd/au"
    echo "   - yuv4mpeg2     - real/realaudio"
    echo "   - ea wve        - raw dv"
    echo "   - interplay mve - psx str"
    echo "   - ws aud        - pva"
    echo "   - vox           - nsf"
    echo "   - nsv           - 4xm"
    echo "   - FLAC          - aac"
    echo "   - iff           - matroska"
    echo "   - vmd           - flv"
    if test x"$enable_a52dec" != x"no"; then
        if test x"$have_external_a52dec" = x"yes"; then
            echo "   - ac3 (external library)"
        else
            echo "   - ac3 (*INTERNAL* library)"
        fi
    fi
    test x"$enable_asf" != x"no"     && echo "   - asf"
    test x"$enable_nosefart" != xno  && echo "   - Nosefart (NSF)"
    test x"$enable_mng" != x"no"     && echo "   - mng"
    test x"$have_modplug" = x"yes"   && echo "   - mod"
    test x"$have_libflac" = x"yes"   && echo "   - FLAC (with libFLAC)"
    test x"$have_vorbis" = x"yes"    && echo "   - ogg"
    test x"$have_wavpack" = x"yes"   && echo "   - WavPack"
    test x"$have_avformat" = x"yes"  && echo "   - avformat (with libavformat)"
    echo ""

    dnl video decoders
    echo " * video decoder plugins:"
    echo "   - MPEG 1,2         - Amiga Bitplane"
    echo "   - Raw RGB          - Raw YUV"
    test x"$have_dxr3" = x"yes"        && echo "   - dxr3_video"
    test x"$have_gdkpixbuf" = x"yes"   && echo "   - gdk-pixbuf"
    test x"$have_imagemagick" = x"yes" && echo "   - image"
    test x"$have_libjpeg" = x"yes"     && echo "   - libjpeg"
    test x"$have_theora" = x"yes"      && echo "   - theora"
    test x"$have_w32dll" = x"yes"      && echo "   - w32dll"
    test x"$have_vdpau" = x"yes"       && echo "   - vdpau"
    test x"$have_mmal" = x"yes"        && echo "   - mmal (Broadcom HW)"
    test x"$have_vpx" = x"yes"         && echo "   - libvpx (VP8/VP9)"
    if test x"$with_external_ffmpeg" != x"no"; then
        echo "   - ffmpeg (external library)"
    else
        echo "   - ffmpeg (*INTERNAL* library):"
        echo "     - MPEG-4 (ISO, Microsoft, DivX*, XviD)"
        echo "     - Creative YUV    - Motion JPEG"
        echo "     - Cinepak         - MS Video-1"
        echo "     - FLI/FLC         - MS RLE"
        echo "     - Id RoQ          - Id Cin"
        echo "     - Apple Graphics  - Apple Video"
        echo "     - Apple Animation - Interplay Video"
        echo "     - Westwood VQA    - Origin Xan"
        echo "     - H.263           - Intel Indeo 3"
        echo "     - SVQ1            - SVQ3"
        echo "     - Real Video 1.0  - Real Video 2.0"
        echo "     - 4X Video        - Sierra Video"
        echo "     - Asus v1/v2      - HuffYUV"
        echo "     - On2 VP3         - DV"
        echo "     - 8BPS            - Duck TrueMotion v1"
        echo "     - ATI VCR1        - Flash Video"
        echo "     - ZLIB            - MSZH"
    fi
    echo ""

    dnl audio decoders
    echo " * audio decoder plugins:"
    echo "   - GSM 06.10       - linear PCM"
    test x"$enable_faad" != x"no"  && echo "   - faad"
    test x"$enable_nosefart" != xno && echo "   - Nosefart (NSF)"
    test x"$have_libflac" = x"yes" && echo "   - FLAC (with libFLAC)"
    test x"$have_speex" = x"yes"   && echo "   - speex"
    test x"$have_vorbis" = x"yes"  && echo "   - vorbis"
    test x"$have_w32dll" = x"yes"  && echo "   - w32dll"
    test x"$have_wavpack" = x"yes" && echo "   - WavPack"
    if test x"$enable_mad" != x"no"; then
        if test x"$have_external_libmad" = x"yes"; then
            echo "   - MAD (MPG 1/2/3) (external library)"
        else
            echo "   - MAD (MPG 1/2/3) (*INTERNAL* library)"
        fi
    fi
    if test x"$enable_libdts" != x"no"; then
        if test x"$have_external_dts" = x"yes"; then
            echo "   - DTS (external library)"
        else
            echo "   - DTS (*INTERNAL* library)"
        fi
    fi
    if test x"$enable_a52dec" != x"no"; then
        if test x"$have_external_a52dec" = x"yes"; then
            echo "   - A52/ra-dnet (external library)"
        else
            echo "   - A52/ra-dnet (*INTERNAL* library)"
        fi
    fi
    if test x"$enable_musepack" != x"no"; then
        if test x"$have_external_libmpcdec" = x"yes"; then
            echo "   - Musepack (external library)"
        else
            echo "   - Musepack (*INTERNAL* library)"
        fi
    fi
    if test x"$with_external_ffmpeg" != x"no"; then
        echo "   - ffmpeg (external library)"
    else
        echo "   - ffmpeg (*INTERNAL* library):"
        echo "     - Windows Media Audio v1/v2/Pro"
        echo "     - DV            - logarithmic PCM"
        echo "     - 14k4          - 28k8"
        echo "     - MS ADPCM      - IMA ADPCM"
        echo "     - XA ADPCM      - Game DPCM/ADPCM"
        echo "     - Mace 3:13     - Mace 6:1"
        echo "     - FLAC"
    fi
    echo ""

    dnl spu decoders
    echo " * subtitle decoder plugins:"
    echo "   - spu             - spucc"
    echo "   - spucmml         - sputext"
    echo "   - spudvb"
    test x"$have_dxr3" = x"yes" && echo "   - dxr3_spu"
    echo ""

    dnl post plugins
    echo " * post effect plugins:"
    echo "  * planar video effects:"
    echo "   - invert          - expand"
    echo "   - eq              - eq2"
    echo "   - boxblur         - denoise3d"
    echo "   - unsharp         - tvtime"
    test x"$have_dvb" = x"yes"      && echo "   - vdr"
    echo "  * SFX:"
    echo "   - goom            - oscope"
    echo "   - fftscope        - mosaico"
    echo ""

    dnl Video plugins
    echo " * video driver plugins:"
    if test x"$no_x" != x"yes"; then
        echo "   - XShm (X11 shared memory)"
        if test x"$have_xv" = x"yes"; then
            if test x"$have_xv_static" = x"yes"; then
                echo "   - Xv (XVideo *static*)"
            else
                echo "   - Xv (XVideo *shared*)"
            fi
        fi
        if test x"$have_xxmc" = x"yes"; then
            if test x"$have_vldexts" = x"yes"; then
                echo "   - XxMC (XVideo extended motion compensation)"
            else
                echo "   - XxMC (XVideo motion compensation - vld extensions DISABLED)"
            fi
        fi
        if test x"$have_xvmc" = x"yes"; then
            echo "   - XvMC (XVideo motion compensation)"
        fi
        if test x"$have_opengl" = x"yes"; then
            if test x"$have_glu" = x"yes"; then
                echo "   - OpenGL (with GLU support)"
            else
                echo "   - OpenGL"
            fi
        fi
        if test x"$have_opengl2" = x"yes"; then
            echo "   - OpenGL 2.0 (with bicubic scaling)"
        fi
        test x"$have_vaapi" = x"yes"       && echo "   - vaapi (Video Acceleration (VA) API for Linux)"
        test x"$have_vdpau" = x"yes"       && echo "   - vdpau (X11 Video Decode and Presentation API for Unix)"
        if test x"$have_sunfb" = x"yes"; then
            if test x"$have_sundga" = x"yes"; then
                echo "   - PGX64 (for Sun XVR100/PGX64/PGX24 cards)"
                echo "   - PGX32 (for Sun PGX32 cards)"
            fi
        fi
    fi
    if test x"$have_xcb" = x"yes"; then
        if test x"$have_xcbshm" = x"yes"; then
            echo "   - xcb-shm (X shared memory using XCB)"
        fi
        if test x"$have_xcbxv" = x"yes"; then
            echo "   - xcb-xv (XVideo using XCB)"
        fi
    fi


    test x"$have_aalib" = x"yes"        && echo "   - aa (Ascii ART)"
    test x"$have_caca" = x"yes"         && echo "   - caca (Color AsCii Art)"
    test x"$have_directfb" = x"yes"     && echo "   - directfb (DirectFB driver)"
    test x"$have_directx" = x"yes"      && echo "   - directx (DirectX video driver)"
    test x"$have_fb" = x"yes"           && echo "   - fb (Linux framebuffer device)"
    test x"$have_libstk" = x"yes"       && echo "   - stk (Libstk Set-top Toolkit)"
    test x"$have_macosx_video" = x"yes" && echo "   - Mac OS X OpenGL"
    test x"$have_sdl" = x"yes"          && echo "   - sdl (Simple DirectMedia Layer)"
    test x"$have_mmal" = x"yes"         && echo "   - mmal (Broadcom MultiMedia Abstraction Layer)"


    if test x"$have_dxr3" = x"yes"; then
        if test x"$have_encoder" = x"yes"; then
            echo "   - dxr3 (Hollywood+ and Creative dxr3, both mpeg and non-mpeg video)"
        else
            echo "   - dxr3 (Hollywood+ and Creative dxr3, mpeg video only)"
        fi
    fi
    if test x"$have_vidix" = x"yes"; then
        echo $ECHO_N "   - vidix ("

        if test x"$no_x" != x"yes"; then
            echo $ECHO_N "X11"
            if test x"$have_fb" = x"yes"; then
                echo $ECHO_N " and "
            fi
        fi

        if test x"$have_fb" = x"yes"; then
            echo $ECHO_N "framebuffer"
        fi

        echo $ECHO_N " support"

        if test x"$enable_dha_kmod" != x"no"; then
            echo " with dhahelper)"
        else
            echo ")"
        fi
    fi

    echo ""

    dnl Audio plugins
    echo " * audio driver plugins:"
    test x"$have_alsa" = x"yes"         && echo "   - alsa (ALSA - Advanced Linux Sound Architecture)"
    test x"$have_coreaudio" = x"yes"    && echo "   - CoreAudio (Mac OS X audio driver)"
    test x"$have_directx" = x"yes"      && echo "   - directx (DirectX audio driver)"
    test x"$have_esound" = x"yes"       && echo "   - esd (Enlightened Sound Daemon)"
    test x"$have_fusionsound" = x"yes"  && echo "   - fusionsound (FusionSound driver)"
    test x"$am_cv_have_irixal" = x"yes" && echo "   - irixal (Irix audio library)"
    test x"$have_jack" = x"yes"         && echo "   - Jack"
    test x"$have_oss" = x"yes"          && echo "   - oss (Open Sound System)"
    test x"$have_pulseaudio" = x"yes"   && echo "   - pulseaudio (PulseAudio sound server)"
    test x"$have_sunaudio" = x"yes"     && echo "   - sun (Sun audio interface)"
    test "x$have_sndio" = "xyes"	&& echo "   - sndio"
    echo "---"


    dnl ---------------------------------------------
    dnl some user warnings
    dnl ---------------------------------------------

    dnl some levels of variable expansion to get final install paths
    final_libdir="`eval eval eval eval echo $libdir`"
    final_bindir="`eval eval eval eval echo $bindir`"

    if test -r /etc/ld.so.conf && ! grep -x "$final_libdir" /etc/ld.so.conf >/dev/null ; then
        if test "$final_libdir" != "/lib" -a "$final_libdir" != "/usr/lib" ; then
            if ! echo "$LD_LIBRARY_PATH" | egrep "(:|^)$final_libdir(/?:|/?$)" >/dev/null ; then
                echo
                echo "****************************************************************"
                echo "xine-lib will be installed to $final_libdir"
                echo
                echo "This path is not mentioned among the linker search paths in your"
                echo "/etc/ld.so.conf. This means it is possible that xine-lib will"
                echo "not be found when you try to compile or run a program using it."
                echo "If this happens, you should add $final_libdir to"
                echo "the environment variable LD_LIBRARY_PATH like that:"
                echo
                echo "export LD_LIBRARY_PATH=$final_libdir:\$LD_LIBRARY_PATH"
                echo
                echo "Alternatively you can add a line \"$final_libdir\""
                echo "to your /etc/ld.so.conf."
                echo "****************************************************************"
                echo
            fi
        fi
    fi

    if ! echo "$PATH" | egrep "(:|^)$final_bindir(/?:|/?$)" >/dev/null ; then
        echo
        echo "****************************************************************"
        echo "xine-config will be installed to $final_bindir"
        echo
        echo "This path is not in your search path. This means it is possible"
        echo "that xine-config will not be found when you try to compile a"
        echo "program using xine-lib. This will result in build failures."
        echo "If this happens, you should add $final_bindir to"
        echo "the environment variable PATH like that:"
        echo
        echo "export PATH=$final_bindir:\$PATH"
        echo
        echo "Note that this is only needed for compilation. It is not needed"
        echo "to have xine-config in your search path at runtime. (Although"
        echo "it will not cause any harm either.)"
        echo "****************************************************************"
        echo
    fi

    dnl warn if no X11 plugins will be built
    if test "x$no_x" = "xyes"; then
        case $host in
            *mingw*|*-cygwin) ;;
            *-darwin*) ;;
            *)
                echo
                echo "****************************************************************"
                echo "WARNING! No X11 output plugins will be built."
                echo
                echo "For some reason, the requirements for building the X11 video"
                echo "output plugins are not met. That means, that you will NOT be"
                echo "able to use the resulting xine-lib to watch videos in a window"
                echo "on any X11-based display (e.g. your desktop)."
                echo
                echo "If this is not what you want, provide the necessary X11 build"
                echo "dependencies (usually done by installing a package called"
                echo "XFree86-devel or similar) and run configure again."
                echo "****************************************************************"
                echo
                ;;
        esac
    fi
])
