import bluesky

bluesky.network.init()
bluesky.network.listen("127.0.0.1",7878)
bluesky.network.start()