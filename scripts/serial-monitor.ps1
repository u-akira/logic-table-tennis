param(
[string]$Port = "COM3",
[int]$Baudrate = 115200
)

arduino-cli monitor `    -p $Port`
-c baudrate=$Baudrate
