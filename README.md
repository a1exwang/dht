# Yet Another BitTorrent DHT Implementation in C++

## TODOs
- Maybe we can use a public VPS as DHT proxy,
  and use local PC as BT client. 
  In this way, we can save the bandwidth of the public VPS,
  and have a public IP for DHT.
  
- Store info hash in a file and prevent duplicates

- Enable clang-tidy. Check for bugprone-use-after-move

- Save torrent file to disk

- Ban multiple node ID with single endpoint