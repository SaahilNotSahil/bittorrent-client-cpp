# BitTorrent Client
Basic BitTorrent client in C++. Supports only single-file torrents.

## How to run:
- Make sure you have a c++ compiler (`g++`/`clang`), `make`, and `cmake` installed on your computer (Linux/MacOS).
- Clone this repository:
```
git clone https://github.com/SaahilNotSahil/bittorrent-client-cpp.git
```
- `cd` into the project folder, and make sure to copy your torrent file as well. Note: Make sure to only use a single-file torrent, as multi-file torrents and magnet links are not supported yet.
```
cd bittorrent-client-cpp
```
- To download the file using the torrent, run the following command:
```
./run.sh download -o <output_file_path> <torrent_file_path>
```
- A sample torrent file `sample.torrent` is provided with the repository.
```
./run.sh download -o file.md sample.torrent
```
