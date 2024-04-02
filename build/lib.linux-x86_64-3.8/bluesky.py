import bluesky
import time

def AcceptCB(fd):
    print("有新的连接",fd)

def OnDisconnectCB():
    pass

def OnDataRecvCB(fd,datas):
    print("python接收数据",fd,datas)
    bluesky.network.write_data(fd,datas)

def TimerOut():
    print("结束定时器",time.time())

bluesky.timer.init()
bluesky.network.init(AcceptCB,OnDisconnectCB,OnDataRecvCB)
bluesky.network.listen("127.0.0.1",7878)
print("开始定时器",time.time())
timerID = bluesky.timer.timerOnce(500,TimerOut)
bluesky.server.start()
