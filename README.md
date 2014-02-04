rpi-webcam
==========

rpi-webcam is a simple server that listen on port 9000 and take snapshots from a webcam, compress in JPEG and send it as response.

The protocol only support 2 commands:
-*f* retrieves a frame.
-*q* terminate the server.

You can send commands easily with nc:

Take a snapshot:
<pre>
echo 'f' | nc localhost 9000 > snapshot.jpeg
</pre>

Close the server:
<pre>
echo 'f' | nc localhost 9000 > snapshot.jpeg
</pre>

= Compilation =

Compile rpi-webcam is easy, the only you could do is:
<pre>
make clean release
</pre>

If you want to use another libjpeg like libjpeg-turbo, you can specify the base path with:
<pre>
LIBJPEG=/opt/libjpeg-turbo make clean release
</pre>

And to use the OpenMax interface and let the GPU work:
<pre>
MODE=OMX make clean release
</pre>

The binary will be under de bin folder.
