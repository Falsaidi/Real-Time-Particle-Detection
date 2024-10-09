import os
import socket
from time import sleep
from time import perf_counter
from tqdm import tqdm

BUFFERSIZE = 1035
TIMEDELAY = 0.005

# Create a socket object
client_socket = socket.socket(socket.AF_INET, socket.SOCK_STREAM)

# Get the hostname of the server machine
server_host = "10.10.1.10"

# Define the port to connect to
server_port = 12345

# Connect to the server
client_socket.connect((server_host, server_port))

# Send data to the server
fileSize = os.stat("data.bin")  # Reads from the binary data
pbar = tqdm(total=fileSize.st_size)  # Initializes a progress bar

# Sends all data
with open("noHeader.bin", "rb") as f:
	# Keeps sending data until the file is empty
	while True:
		packet = f.read(BUFFERSIZE)  # Reads the bin file in packets 
		if not packet:  # If the file is empty
			break
		client_socket.send(packet)  # Sends data over ethernet
		pbar.update(1036)
		sleep(TIMEDELAY)
	pbar.close()

print("data sent")
