#!/bin/sh
#export SDL_AUDIODRIVER=dsp
#export SIZE=640x360
#export SIZE=640x480
#export SIZE=688x384
export SIZE=160x50
#export SIZE=240x160

#export FREQ=48000

#export FPS=10
#export FPS=18
export FPS=20
#export FPS=30
#export FPS=60

cc -O3 -o player tools/player.c `sdl2-config --cflags --libs` && \
make -j8 && \
echo OK && \
ffmpeg -v 0 $5 -i "$1" $3 -r $FPS -f matroska -acodec aac -strict -2 -ar 48000 -vcodec rawvideo -pix_fmt bgr24 - 2>/dev/null | \
./ice2 -d -i /dev/stdin | \
cat - > $2

#./player /dev/stdin

#ffmpeg -v 0 $5 -i "$1" $3 -s $SIZE -r $FPS -f matroska -acodec copy -strict -2 -vcodec rawvideo -pix_fmt bgr24 - 2>/dev/null | \
#ffplay -

#./ice2 -d -i /dev/stdin | \
#./player /dev/stdin

#./ice2 -d -i "$1" | \

#ffmpeg -v 0 $5 -i "$1" $3 -s $SIZE -r $FPS -f rawvideo -pix_fmt rgb24 - 2>/dev/null | \
#ffmpeg -v 0 -f rawvideo -pix_fmt rgb24 -s $SIZE -r $FPS -i - $5 $6 -vn -i "$1" -map 0:0 -map "1$2" $4 -f avi -acodec mp3 -vcodec rawvideo -r $FPS -s $SIZE -pix_fmt bgr24 - 2>/dev/null | \
#ffplay -v 0 -vf setdar=1.78 - 2>/dev/null
#mpv - 2>/dev/null
#mpv -monitorpixelaspect 2 - 2>/dev/null
#mpv -monitorpixelaspect 0.8 - 2>/dev/null
#ffplay -v 0 -vf setdar=1.6 - 2>/dev/null

#ffmpeg -v 0 $5 -i "$1" $3 -s $SIZE -r $FPS -f rawvideo -pix_fmt rgb24 - 2>/dev/null | \

#mpv -monitorpixelaspect 0.8 - 2>/dev/null
#mpv -monitorpixelaspect 2 - 2>/dev/null
#ffplay -v 0 -vf setdar=1.6 - 2>/dev/null
#ffplay -v 0 -vf setdar=1.78 - 2>/dev/null
#ffplay -v 0 -vf setdar=1.78 -

# ~/Desktop/appage/tiny/ldcirno7 | \
#

