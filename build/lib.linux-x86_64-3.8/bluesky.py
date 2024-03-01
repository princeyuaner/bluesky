import bluesky

def AcceptCB(fd):
    print("有新的连接",fd)

def OnDisconnectCB():
    pass

def OnDataRecvCB(fd,datas):
    print("python接收数据",fd,datas)
    bluesky.network.write_data(fd,datas)

bluesky.network.init(AcceptCB,OnDisconnectCB,OnDataRecvCB)
bluesky.network.listen("127.0.0.1",7878)
bluesky.server.start()