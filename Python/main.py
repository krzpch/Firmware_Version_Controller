from multiprocessing import Process, Queue, Event
import os
import sys
import time
import signal

import fvc_protocol
from struct import pack

from fvc_hash import hmac_calc
from usart_process import SerialProcess

_port = "COM6"
_baudrate = 115200

_timeout_value_ns = 5*pow(10,6) # 5 s
_max_retransfers = 5

_hmac_key = b'secret_key'

max_data_size = 2*1024

def calc_packet_quantity(data_size):
    packet_quantity = 0
    while (data_size > 0):
        packet_quantity += 1
        data_size = data_size - max_data_size
    return packet_quantity

def parseData(rxQueue: Queue, endEvent: Event):
    timeout = time.time_ns() + _timeout_value_ns
    
    while time.time_ns < timeout or endEvent.is_set():
        if not rxQueue.empty():
            data = fvc_protocol.deserialzie_packet(rxQueue.get(timeout=0.1))
            match data[4]:
                case fvc_protocol.data_types.TYPE_CLI_DATA:
                    print("CLI (SRC:", data[2] ," -> DEST:", data[3] ," )", data[5])
                case other:
                    return data
    return None

def boardUpdateProcess(boardID: int, programPath: str, txQueue: Queue, rxQueueu: Queue, endEvent: Event):
    timer_start = time.time_ns()
    state = 0
    update_status = False
    retransfers_counter = 0
    
    program_packet = None
    hmac_sha = None
    packet_count = None
    
    with open(programPath, "rb") as file:
        data_size = os.path.getsize(programPath)
        program_data = file.read(data_size)
        packet_count = calc_packet_quantity(data_size)
        hmac_sha = hmac_calc(program_data, _hmac_key)
        
    with open(programPath, "rb") as file:
        while not endEvent.is_set():
            match state:
                case 0: # Update request
                    data = fvc_protocol.serialize_packet(fvc_protocol.data_types.TYPE_PROGRAM_UPDATE_REQUEST, int(id), pack(">LL32s", 1, packet_count, hmac_sha))
                    txQueue.put(data)
                    
                    data = parseData(rxQueueu, endEvent)
                    if data != None and data[4] == fvc_protocol.data_types.TYPE_ACK:
                        state = 1
                    if data != None and data[4] == fvc_protocol.data_types.TYPE_NACK:
                        print("Board responded with NACK! Update aborted.")
                        endEvent.set()
                    else:
                        endEvent.set()
                case 1: # Prepare packet 
                    retransfers_counter = 0
                    program_data = file.read(max_data_size)
                    if len(program_data) > 0:
                        program_packet = fvc_protocol.serialize_packet(fvc_protocol.data_types.TYPE_PROGRAM_DATA, int(id), program_data)
                        state = 2
                    else: # finish update if there is no more data to be send
                        update_status = True
                        endEvent.set()
                case 2: # send packet
                    if retransfers_counter < _max_retransfers:
                        txQueue.put(program_packet)
                        
                        data = parseData(rxQueueu, endEvent)
                        if data != None and data[4] == fvc_protocol.data_types.TYPE_ACK:
                            state = 1
                        if data != None and data[4] == fvc_protocol.data_types.TYPE_NACK:
                            state = 2
                            retransfers_counter += 1
                        else:
                            endEvent.set()
                    else:
                        endEvent.set()
            
    if update_status:
        print("Update finished for board with ID:", boardID," (Took:", (time.time_ns() - timer_start)/1000000 ,"ms)")
    else:
        print("Update failed for board with ID:", boardID," (Took:", (time.time_ns() - timer_start)/1000000 ,"ms)")

def parseDataProcess(uartQueueRx: Queue, uartQueueTx: Queue, updateQueueDictRx: dict, updateQueueDictTx: dict, endEvent: Event):
    boards_id = updateQueueDictRx.keys()
    
    while not endEvent.is_set():
        if not uartQueueRx.empty():
            data = uartQueueRx.get()
            packet = fvc_protocol.deserialzie_packet(data)
            if packet != None:
                if packet[2] in boards_id:
                    updateQueueDictRx[packet[2]].put(data)
                else:
                    print("Got data form unsupported board! (ID:", packet[2], ") DATA:", packet)
            
        for id in boards_id:
            if not updateQueueDictTx[id].empty():
                data = updateQueueDictTx[id].get()
                uartQueueTx.put(data)
        
def main(paralelUpdateEn: bool): 
    processList = []
    txQueuesDict = dict()
    rxQueuesDict = dict()
    updateEndEventDict = dict()
    boards_to_update =[]
    
    # SIGINT signal handling function
    def endProcessesSignalHandler(*args):
        print("Got SIGINT signal. Closing update processes")
        for event in updateEndEventDict:
            event.set()
    
    signal.signal(signal.SIGINT, endProcessesSignalHandler)
    
    # parse input data
    input_args = sys.argv[1:]
    
    if len(input_args) < 2:
        print("ERROR: Not enough arguments\nUsage: main.py boards_id.txt program.bin") 
        return
    
    with open(input_args[0], "r") as boards:
        lines = boards.readlines()
        for line in lines:
            boards_to_update.append(line)
            print("Added ID:", line)
    
    program_path = input_args[1]
    
    # serila port queues and event
    serialPortRxQueue = Queue(100)
    serialPortTxQueue = Queue(100)
    serialPortCloseEvent = Event()
    
    # parser end event
    parserCloseEvent = Event()
    
    # prepare serial port and update process
    serialPortProcess = Process(target=SerialProcess, args=(serialPortTxQueue,serialPortRxQueue,serialPortCloseEvent,_port,_baudrate))
    
    # prepare update processes
    for id in boards_to_update:
        txQueuesDict[id] = Queue(100)
        rxQueuesDict[id] = Queue(100)
        updateEndEventDict[id] = Event()
        processList.append(Process(target=boardUpdateProcess, args=(id, program_path, txQueuesDict[id], rxQueuesDict[id], updateEndEventDict[id])))
    
    # prepare parser process
    parserProcess = Process(target=parseDataProcess,args=(serialPortRxQueue,serialPortTxQueue,rxQueuesDict,txQueuesDict))
    
    # starting uart and parser processes
    serialPortProcess.start()
    parserProcess.start()
    

    if paralelUpdateEn: # updating all boards at th same time 
        # start processes
        print("Starting update processes.")
        for proc in processList:
            proc.start()
        
        # wait for all updates to be completed or not responding
        for proc in processList: # skip serial port and parser processes
            proc.join()
    else: # updating one board at a time
        for proc in processList: 
            proc.start()
            proc.join()
    
    # send signal to end serial prot and parser processes
    print("Closing parser and serial prot porcesses.")
    parserCloseEvent.set()
    serialPortCloseEvent.set()
    
    # wait for end of proceese
    parserProcess.join()
    serialPortProcess.join()
    
    # print statistics about update
    print("Update statistics")
    

if __name__ == "__main__":
    main(False)