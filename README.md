# IPoverObexPC - BT terminal server

## Linux
You need to register D-Bus service by copying ```IPoverObex.conf``` to ```/etc/dbus-1/system.d/```

Also you need to stop obex.service, you can use ```systemctl --user mask obex.service```


sudo cp ./IPoverObexPC /usr/local/bin/
sudo cp ./ipoverobexpc.service /etc/systemd/system/
sudo systemctl daemon-reload
sudo systemctl enable ipoverobexpc
sudo systemctl start ipoverobexpc


## File

- [IPoverObexPC](https://rdzdx.github.io/IPoverObexPC/IPoverObexPC) 32 bit binary Raspberry Pi ZeroW Bookworm
