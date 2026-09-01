# e-ink-photo-frame
A low-power ESP32-S3 e-paper digital frame using a 13.3" E Ink Spectra 6 display, paired with a web REST API for server-side image dithering and automatic updates.

for the function of the ESP32 controller
1. able to connect home's wifi
2. after connected to wifi, connect to the website and call the REST api in the webserver to see if any new images updated.
3. if have new images updated (should be binary and dithered, and no need to do extra image process for the ESP32 to output to the e-ink), download them and store them
4. deep sleep (cut the wifi) and wait for may be 6 hours to wake up again and do the procedures again
5. can wake up and do the procedures again by pressing the hard button (IO12 )

for the function of the webserver and REST API
1. webpage to update image, preview the output (using RGB color that similar to the E6 e-ink's color) of the dithered image with difficult algorithms
2. store the original image and the dithered binary to the server
3. API for the list of images
4. API for ESP32 to download dithered image's binary
5. 
