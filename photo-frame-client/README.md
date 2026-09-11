Wi-Fi Easy Connect

1. a QR code will appear on your console.
2. Connect your phone to the network, say named "Example-AP".
3. If in Android: go to Settings->WiFi & Internet->Wi-Fi->Example-AP->Advanced->Add Device.
4. Scan QR Code using the scanner, which will make ESP device connect to Example-AP.
5. It will discover the server's IP address and port through mutlicast.
6. Then it will get an image from the server and display it on the e-ink display.

TODO:
	Server:
		1. upload image
			a. upload image, do dithering with different algorithms
			b. display the simulated result on the web page (using e-ink display color)
			c. if ok, upload to server (original and dithered image) (choose from list of devices, or default)
			d. convert the dithered image to bin for E6 display. store the original, simulate and bin files
		2. server manage devices (MAC) and their list of images
		3. 
			a. client send the list of images to server:
			{
			  "mac": "24:DC:C3:A1:B2:C3",
			  "local_files": [
			    { "name": "photo01.bin", "size": 96000 },
			    { "name": "photo02.bin", "size": 96000 }
			  ]
			}
			
			b. server compare the list with it's managed list and return the changes
			{
			  "new": [
			    { "name": "photo03.bin", "url": "/api/images/photo03.bin" }
			  ],
			  "delete": [
			    "photo01.bin"
			  ]
			}
			
			c. client delete the images from local according to the list.
				then request new images one by one
				
			d. rotate and display the images every time interval