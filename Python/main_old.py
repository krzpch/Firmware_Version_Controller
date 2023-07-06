import multiprocessing
import serial
from queue import Queue
import sys
import os
import signal
from struct import pack, unpack
import time

import fvc_protocol

_port = "COM6"
_baudrate = 115200

boards = ".\\boards_id.txt"
program = ".\\default_firmware_short.bin"

max_data_size = 2*1024
# max_data_size = 256

update_timeout_value = 300 # timeout for boards in s

def hash_calc(data) -> int:
    return 0

def calc_packet_quantity(data_size):
    packet_quantity = 0
    while (data_size > 0):
        packet_quantity += 1
        data_size = data_size - max_data_size
    return packet_quantity

# def serial_read_thread(uart, queue):
#     while uart.is_open:
#         if not pause:
#             if uart.in_waiting > 0:
#                 data = uart.read_all()
#                 queue.put(data)

def wait_for_ack(serial_prot: serial.Serial):
    while True:
        buffer = bytes()
        while (len(buffer) < 4):
            buffer += serial_prot.read_all()
        # print("Data:", buffer)
        deserialized_data = fvc_protocol.deserialzie_packet(buffer)
        if deserialized_data is not None:          
            if(deserialized_data[2] == int(fvc_protocol.data_types.TYPE_ACK)):
                return True
            elif (deserialized_data[2] == int(fvc_protocol.data_types.TYPE_NACK)):
                return False
        else:
            return False

def Program_board_with_id(id: int, data_size: int, crc, packet_count: int):
    uart = serial.Serial(port=_port, baudrate=_baudrate)
    p_counter = 0
    
    timer_start = time.time()
    data = fvc_protocol.serialize_packet(fvc_protocol.data_types.TYPE_PROGRAM_UPDATE_REQUEST, int(id), pack(">LLL", 1, packet_count, crc))
    uart.write(data)
    
    if not wait_for_ack(uart):
        ret_val = -1
        return

    with open(program, "rb") as file:
        data = file.read(max_data_size)
        while p_counter < packet_count:
            packet_timer_start = time.time()
            print("Sending",p_counter,"packet")
            packet = fvc_protocol.serialize_packet(fvc_protocol.data_types.TYPE_PROGRAM_DATA, int(id), data)
            uart.write(packet) 
            
            if wait_for_ack(uart):
                p_counter += 1
                data = file.read(max_data_size)
                print("Packet transmitted correctly")
            else:
                print("Packet transmitted incorrectly")

            print("Packet time:", time.time() - packet_timer_start ,"s")
    
    print("Update finished (Took:", time.time() - timer_start ,"s)")

# def signal_handler(sig, frame):
#     print("process stoped ^C")
#     while (len(thread_list)):
#         thread = thread_list.pop()
#         thread.join(timeuot=0)
#     uart.close()
#     sys.exit(0)

def main():
    global thread_list

    thread_list = []
    # signal.signal(signal.SIGINT, signal_handler)
    
    data_queue = Queue(maxsize = 50)
    
    # args = sys.argv[1:]
    
    # if len(args) < 2:
    #     print("ERROR: Not enough arguments\nUsage: main.py boards_id.txt program.bin") 
    #     return
        
    # # Handle board file and prepare list of boards IDs
    boards_to_update =[]
    data_size = 0
    pause = False
    
    # with open(args[0], "r") as boards:
    #     lines = boards.readlines()
    #     for line in lines:
    #         boards_to_update.append(line)
    
    with open(boards, "r") as boards_ids:
        lines = boards_ids.readlines()
        for line in lines:
            boards_to_update.append(line)

    if len(boards_to_update) == 0:
        print("Boards list empty")
        return
    
    with open(program, "rb") as file:
        data_size = os.path.getsize(program)
        program_data = file.read(data_size)
        packet_count = calc_packet_quantity(data_size)
        program_crc = hash_calc(program_data)
    
    for id in boards_to_update:
        print("Updating board with ID:", id)
        update = multiprocessing.Process(target=Program_board_with_id, args=(id,data_size,program_crc,packet_count,))
        update.start()
        update.join(timeout=update_timeout_value)
        update.terminate()

    # while uart.is_open:
    #     if not data_queue.empty():
    #         data = data_queue.get()
    #         deserialized_data = fvc_protocol.deserialzie_packet(data)
    #         if len(deserialized_data) > 4:
    #             (src_ID, dest_ID, data_t, payload_len, payload, crc) = deserialized_data
    #             print("SRC:", src_ID, "DEST:", dest_ID, "TYPE:",data_t, "DATA:", payload)
    #         else:
    #             (src_ID, dest_ID, data_t, crc) = deserialized_data
    #             print("SRC:", src_ID, "DEST:", dest_ID, "TYPE:",data_t)
                
    # uart.close()
            

if __name__ == "__main__":
    main()