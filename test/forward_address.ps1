netsh interface portproxy add v4tov4 listenport=22223 listenaddress=127.0.0.1 connectport=22223 connectaddress=172.25.192.2

# Chrome flags to use QUIC for localhost
# --origin-to-force-quic-on=localhost:22223
# --origin-to-force-quic-on=linuxvm:22223