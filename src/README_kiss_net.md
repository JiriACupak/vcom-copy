# KISS over network

The KISS_NET interface is used to connect to the remote serial port over network. The serial port might be exported via [ser2net](https://sourceforge.net/projects/ser2net/). KISS protocol is used for the communication on the underlying serial over network connection.

Example of the ser2net invocation (running on port 2000 and forwarding /dev/ttyUSB0 115200 8N1):

    ser2net -d -n -Y 'connection: &con01#  accepter: tcp,2000' -Y '  connector: serialdev(nouucplock=true),/dev/ttyUSB0,115200n81,local'

Please note that plain tcp (raw) accepter is crucial. The telnet (rfc2217) is not supported and it doesn't work at all.

You need only the `host_ip` and `host_port` in the vcom configuration. The interface name is `KISS_NET`.
