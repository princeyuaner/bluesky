import bluesky

def AcceptCB(fd,addr):
    print("有新的连接",fd,addr)

def OnDisconnectCB():
    pass

def OnDataRecvCB(datas):
    print("python接收数据",datas)

bluesky.network.init(AcceptCB,OnDisconnectCB,OnDataRecvCB)
bluesky.network.listen("127.0.0.1",7878)
bluesky.server.start()