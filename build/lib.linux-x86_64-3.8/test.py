import bluesky

def AcceptCB():
    pass

def OnDisconnectCB():
    pass

def OnDataRecvCB(datas):
    print("python接收数据",datas)

bluesky.network.init(AcceptCB,OnDisconnectCB,OnDataRecvCB)
bluesky.network.listen("127.0.0.1",7878)
bluesky.server.start()