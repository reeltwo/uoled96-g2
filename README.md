# Holoprojector using uOLED-96-G2 #

Tools to create an SD card for the uOLED-96-G2 and a programmer. You will steel need the Workshop4 tools if you wish to modify and recompile the HoloOLED.4ge.

#### Upload

./goldelox_programmer HoloOLED/HoloOLED.4xe

Will reset the uOLED unit and upload the latest HoloOLED.4xe executable. You will need the uUSB-PA5-II programmer or an FTDI cable with voltage divider for the RESET pin. The RESET pin is V3.3 and should NOT be connected directly to the RTS pin on the FTDI cable. People using OSX may need to use an FTDI cable with a voltage divider because the CP201 driver seems to be messed up for OSX.

#### Creating a media file

Requires ffmpeg to be installed

##### OSX

brew install ffmpeg

##### Linux

sudo apt install ffmpeg

#### Creating a media file

./goldelox_media -auto-play:star_wars_logo.mp4 LeiaHolo.mov Plans.mov -out media.bin

Will create a media file that automatically plays the star_wars_logo.mp4 on startup and after that will sit and wait for serial play command. The serial command is just a single byte:

0x00: stop playing
0x01: play first movie (star_wars_logo.mp4)
0x02: play second movie (LeiaHolo.mov)
0x03: play third movie (Plans.mov)

.... etc

./goldelox_media -auto-play-loop -auto-play-delay:50 -auto-play:star_wars_logo.mp4 -autp-play:LeiaHolo.mov -auto-play:Plans.mov -out media.bin

Will create a media file that loop through all three movies with a 5 second pause (50 * 100ms). A serial command can still be sent to select a specific movie, but a serial command will disable auto playback.

#### Flashing media file to SD card

sudo dd bs=512 if=media.bin of=/dev/<your device>

