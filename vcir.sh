#!/bin/sh
#export SDL_AUDIODRIVER=dsp
export SIZE=640x480
#export SIZE=688x384
#export SIZE=160x50
#export SIZE=240x160

#export FPS=20
#export FPS=30
export FPS=60

make -j8 && \
echo OK && \
ffmpeg -v 0 $5 -i "$1" $3 -s $SIZE -r $FPS -f rawvideo -pix_fmt rgb24 - 2>/dev/null | \
./ice2 out.ice2 | \
ffmpeg -v 0 -f rawvideo -pix_fmt rgb24 -s $SIZE -r $FPS -i - $5 $6 -vn -i "$1" -map 0:0 -map "1$2" $4 -f avi -acodec mp3 -vcodec rawvideo -r $FPS -s $SIZE -pix_fmt bgr24 - 2>/dev/null | \
#ffplay -v 0 -vf setdar=1.78 - 2>/dev/null
mpv - 2>/dev/null
#mpv -monitorpixelaspect 2 - 2>/dev/null
#mpv -monitorpixelaspect 0.8 - 2>/dev/null
#ffplay -v 0 -vf setdar=1.6 - 2>/dev/null

#mpv -monitorpixelaspect 0.8 - 2>/dev/null
#mpv -monitorpixelaspect 2 - 2>/dev/null
#ffplay -v 0 -vf setdar=1.6 - 2>/dev/null
#ffplay -v 0 -vf setdar=1.78 - 2>/dev/null
#ffplay -v 0 -vf setdar=1.78 -

# ~/Desktop/appage/tiny/ldcirno7 | \
#

