from multiprocessing import Process, Queue, Event
from serial import Serial

from fvc_protocol import get_packet_len, serialize_packet, deserialzie_packet, data_types

import time

def SerialProcess(tx_queue: Queue, rx_queue: Queue, stop_event: Event, _port: str, _baudrate: int): 
    ser = Serial(port=_port, baudrate=_baudrate)
    if not ser.is_open:
        ser.open()
        
    while ser.is_open and not stop_event.is_set():
        if not rx_queue.full() and ser.in_waiting > 2:
            data = ser.read(3)
            if len(data) == 3:
                data_len_to_read = get_packet_len(data)
                if data_len_to_read != None:
                    data += ser.read(data_len_to_read)
                    rx_queue.put(data, block=True, timeout=0.1)
                    
        if not tx_queue.empty():
            ser.write(tx_queue.get(block=True, timeout=0.1))

    ser.close()


# self test (needs loopback on serial port)
if __name__ == "__main__":
    transmit_queue = Queue(100)
    receive_queue = Queue(100)
    end_process = Event()
    
    sproc = Process(target=SerialProcess, args=(transmit_queue, receive_queue, end_process, 'COM6', 115200))
    sproc.start()
    
    for i in range(10):
        print("data put")
        transmit_queue.put(serialize_packet(data_types.TYPE_CLI_DATA, i, bytes("TEST\n\r", encoding="ascii")))
        time.sleep(1)
        
    while not receive_queue.empty():
        data = deserialzie_packet(receive_queue.get(timeout=5))
        print(data)
        
    end_process.set()
    sproc.join()
    print("Process joined")
    
    