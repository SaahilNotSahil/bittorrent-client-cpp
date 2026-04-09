#include <algorithm>
#include <arpa/inet.h>
#include <cctype>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <curl/curl.h>
#include <fstream>
#include <iostream>
#include <openssl/sha.h>
#include <random>
#include <string>
#include <sys/socket.h>
#include <unistd.h>
#include <vector>

#include "lib/nlohmann/json.hpp"

using json = nlohmann::json;

#define PORT 6881
#define BLOCK_LENGTH 16384

std::string generate_peer_id() {
  const std::string characters =
      "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";

  std::random_device random_device;
  std::mt19937 generator(random_device());
  std::uniform_int_distribution<> distribution(0, characters.size() - 1);

  std::string peer_id;
  for (size_t i = 0; i < 20; ++i) {
    peer_id += characters[distribution(generator)];
  }

  return peer_id;
}

size_t write_callback(void *contents, size_t size, size_t nmemb,
                      std::string *output) {
  output->append((char *)contents, size * nmemb);

  return size * nmemb;
}

std::string http_get(const std::string &url) {
  CURL *curl = curl_easy_init();
  std::string response;

  if (curl) {
    curl_easy_setopt(curl, CURLOPT_URL, url.c_str());
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, write_callback);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);

    CURLcode res = curl_easy_perform(curl);

    if (res != CURLE_OK) {
      std::cerr << "curl error: " << curl_easy_strerror(res) << std::endl;
    }

    curl_easy_cleanup(curl);
  }

  return response;
}

std::pair<json, size_t>
decode_bencoded_string_value(const std::string &encoded_value) {
  if (std::isdigit(encoded_value[0])) {
    // Example: "5:hello" -> "hello"
    size_t colon_index = encoded_value.find(':');

    if (colon_index != std::string::npos) {
      std::string number_string = encoded_value.substr(0, colon_index);
      int64_t number = std::atoll(number_string.c_str());
      std::string str = encoded_value.substr(colon_index + 1, number);

      size_t consumed = colon_index + 1 + number;

      return std::make_pair(json(str), consumed);
    } else {
      throw std::runtime_error("Invalid encoded value: " + encoded_value);
    }
  } else if (encoded_value[0] == 'i') {
    // Example: "i52e" -> 52
    size_t e_index = encoded_value.find('e');

    if (e_index != std::string::npos) {
      std::string number_string = encoded_value.substr(1, e_index - 1);
      int64_t integer = std::atoll(number_string.c_str());

      size_t consumed = e_index + 1;

      return std::make_pair(json(integer), consumed);
    } else {
      throw std::runtime_error("Invalid encoded value: " + encoded_value);
    }
  } else if (encoded_value[0] == 'l') {
    // Example: "l4:spam4:eggse" -> ["spam", "eggs"]
    size_t start = 1;
    json array = json::array();

    while (encoded_value[start] != 'e') {
      std::pair<json, size_t> result =
          decode_bencoded_string_value(encoded_value.substr(start));
      array.push_back(result.first);
      start += result.second;
    }

    size_t consumed = start + 1;

    return std::make_pair(array, consumed);
  } else if (encoded_value[0] == 'd') {
    size_t start = 1;
    json object = json::object();

    while (encoded_value[start] != 'e') {
      std::pair<json, size_t> key_result =
          decode_bencoded_string_value(encoded_value.substr(start));
      std::string key = key_result.first.get<std::string>();
      size_t key_consumed = key_result.second;

      std::pair<json, size_t> value_result = decode_bencoded_string_value(
          encoded_value.substr(start + key_consumed));
      json value = value_result.first;
      size_t value_consumed = value_result.second;

      object[key] = value;
      start += key_consumed + value_consumed;
    }

    size_t consumed = start + 1;

    return std::make_pair(object, consumed);
  } else {
    throw std::runtime_error("Unhandled encoded value: " + encoded_value);
  }
}

std::pair<json, size_t>
decode_bencoded_value(const std::vector<uint8_t> &encoded_value) {
  if (std::isdigit(encoded_value[0])) {
    size_t colon_index = encoded_value.size();

    for (size_t i = 0; i < encoded_value.size(); i++) {
      if (encoded_value[i] == ':') {
        colon_index = i;

        break;
      }
    }

    if (colon_index != std::string::npos) {
      std::string number_string;

      for (size_t i = 0; i < colon_index; i++) {
        number_string += encoded_value[i];
      }

      int64_t number = std::atoll(number_string.c_str());

      std::vector<uint8_t> bytes;
      for (size_t i = colon_index + 1; i < colon_index + 1 + number; i++) {
        bytes.push_back(encoded_value[i]);
      }

      size_t consumed = colon_index + 1 + number;

      return std::make_pair(json::binary(bytes), consumed);
    } else {
      throw std::runtime_error("Invalid encoded value");
    }
  } else if (encoded_value[0] == 'i') {
    size_t e_index = encoded_value.size();

    for (size_t i = 1; i < encoded_value.size(); i++) {
      if (encoded_value[i] == 'e') {
        e_index = i;
        break;
      }
    }

    if (e_index != std::string::npos) {
      std::string number_string;

      for (size_t i = 1; i < e_index; i++) {
        number_string += encoded_value[i];
      }

      int64_t integer = std::atoll(number_string.c_str());

      size_t consumed = e_index + 1;

      return std::make_pair(json(integer), consumed);
    } else {
      throw std::runtime_error("Invalid encoded value");
    }
  } else if (encoded_value[0] == 'l') {
    size_t start = 1;

    json array = json::array();

    while (encoded_value[start] != 'e') {
      std::vector<uint8_t> remaining(encoded_value.begin() + start,
                                     encoded_value.end());
      std::pair<json, size_t> result = decode_bencoded_value(remaining);
      array.push_back(result.first);
      start += result.second;
    }

    size_t consumed = start + 1;

    return std::make_pair(array, consumed);
  } else if (encoded_value[0] == 'd') {
    size_t start = 1;

    json object = json::object();

    while (encoded_value[start] != 'e') {
      std::vector<uint8_t> remaining(encoded_value.begin() + start,
                                     encoded_value.end());
      std::pair<json, size_t> key_result = decode_bencoded_value(remaining);
      std::vector<uint8_t> key_bytes = key_result.first.get_binary();
      std::string key(key_bytes.begin(), key_bytes.end());

      size_t key_consumed = key_result.second;

      remaining = std::vector<uint8_t>(
          encoded_value.begin() + start + key_consumed, encoded_value.end());

      std::pair<json, size_t> value_result = decode_bencoded_value(remaining);

      json value = value_result.first;

      size_t value_consumed = value_result.second;

      object[key] = value;

      start += key_consumed + value_consumed;
    }

    size_t consumed = start + 1;

    return std::make_pair(object, consumed);
  } else {
    throw std::runtime_error("Unhandled encoded value");
  }
}

std::string bencode(json value) {
  if (value.is_number_integer()) {
    return "i" + std::to_string(value.get<int64_t>()) + "e";
  } else if (value.is_string()) {
    const std::string &s = value.get<std::string>();

    return std::to_string(s.size()) + ":" + s;
  } else if (value.is_binary()) {
    const auto &bytes = value.get_binary();

    std::string s(bytes.begin(), bytes.end());

    return std::to_string(s.size()) + ":" + s;
  } else if (value.is_array()) {
    std::string res = "l";

    for (const auto &item : value) {
      res += bencode(item);
    }

    res += "e";

    return res;
  } else if (value.is_object()) {
    std::string res = "d";

    // keys must be sorted lexicographically
    std::vector<std::string> keys;
    for (auto it = value.begin(); it != value.end(); ++it) {
      keys.push_back(it.key());
    }

    std::sort(keys.begin(), keys.end());

    for (const auto &key : keys) {
      res += std::to_string(key.size()) + ":" + key;
      res += bencode(value[key]);
    }

    res += "e";

    return res;
  }

  throw std::runtime_error("Unsupported JSON type for bencoding");
}

json parse_torrent_file(const std::string torrent_file_path) {
  std::ifstream file(torrent_file_path, std::ios::binary);
  if (!file.is_open()) {
    std::cerr << "Failed to open torrent file: " << torrent_file_path
              << std::endl;

    return 1;
  }

  std::vector<uint8_t> content((std::istreambuf_iterator<char>(file)),
                               std::istreambuf_iterator<char>());

  std::pair<json, size_t> result = decode_bencoded_value(content);

  return result.first;
}

std::string url_encode(const std::string &input) {
  std::string output;

  for (unsigned char c : input) {
    if ((c >= 'A' && c <= 'Z') || (c >= 'a' && c <= 'z') ||
        (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
        c == '~') {
      output += c;
    } else {
      char buf[4];

      snprintf(buf, sizeof(buf), "%%%02X", c);

      output += buf;
    }
  }

  return output;
}

std::string url_decode(const std::string &input) {
  std::string output;

  for (size_t i = 0; i < input.size(); i++) {
    if (input[i] == '%' && i + 2 < input.size()) {
      std::string hex = input.substr(i + 1, 2);

      char decoded_char =
          static_cast<char>(std::strtol(hex.c_str(), nullptr, 16));

      output += decoded_char;

      i += 2;
    } else {
      output += input[i];
    }
  }

  return output;
}

std::vector<std::string> get_peers(const std::string announce_url,
                                   const std::string &info_hash,
                                   const int64_t file_length) {
  std::vector<std::string> peers;

  std::string url = announce_url + "?info_hash=" + url_encode(info_hash);
  url += "&peer_id=" + generate_peer_id();
  url += "&port=" + std::to_string(PORT);
  url += "&uploaded=0";
  url += "&downloaded=0";
  url += "&left=" + std::to_string(file_length);
  url += "&compact=1";

  std::string response = http_get(url);

  std::vector<uint8_t> response_vector(response.begin(), response.end());

  std::pair<json, size_t> decoded_response =
      decode_bencoded_value(response_vector);

  auto p = decoded_response.first["peers"].get_binary();

  for (size_t i = 0; i < p.size(); i += 6) {
    std::string ip = std::to_string(p[i]) + "." + std::to_string(p[i + 1]) +
                     "." + std::to_string(p[i + 2]) + "." +
                     std::to_string(p[i + 3]);
    int port = (p[i + 4] << 8) | p[i + 5];

    peers.push_back(ip + ":" + std::to_string(port));
  }

  return peers;
}

std::vector<uint8_t> generate_handshake(const std::string &info_hash,
                                        const uint64_t extension) {
  std::vector<uint8_t> handshake(68);

  handshake[0] = 19;

  std::string fixed = "BitTorrent protocol";
  std::string peer_id = generate_peer_id();

  std::copy(fixed.begin(), fixed.end(), handshake.begin() + 1);

  handshake[20] = (extension >> 56) & 0xFF;
  handshake[21] = (extension >> 48) & 0xFF;
  handshake[22] = (extension >> 40) & 0xFF;
  handshake[23] = (extension >> 32) & 0xFF;
  handshake[24] = (extension >> 24) & 0xFF;
  handshake[25] = (extension >> 16) & 0xFF;
  handshake[26] = (extension >> 8) & 0xFF;
  handshake[27] = extension & 0xFF;

  std::copy(info_hash.begin(), info_hash.end(), handshake.begin() + 28);
  std::copy(peer_id.begin(), peer_id.end(), handshake.begin() + 48);

  return handshake;
}

int main(int argc, char *argv[]) {
  std::cout << std::unitbuf;
  std::cerr << std::unitbuf;

  if (argc < 5) {
    std::cerr << "Usage: " << argv[0]
              << " download -o <output_file> <torrent_file>" << std::endl;

    return 1;
  }

  std::string command = argv[1];

  if (command == "download") {
    std::string output_file = argv[3];
    std::string torrent_file = argv[4];

    json torrent = parse_torrent_file(torrent_file);

    std::vector<uint8_t> announce_bytes = torrent["announce"].get_binary();
    std::string announce_url(announce_bytes.begin(), announce_bytes.end());

    int64_t length = torrent["info"]["length"];
    auto piece_length = torrent["info"]["piece length"];

    std::vector<uint8_t> piece_hashes = torrent["info"]["pieces"].get_binary();

    std::string bencoded_info = bencode(torrent["info"]);
    unsigned char hash[SHA_DIGEST_LENGTH];
    const unsigned char *data =
        reinterpret_cast<const unsigned char *>(bencoded_info.data());
    size_t size = bencoded_info.size();
    SHA1(data, size, hash);

    std::string info_hash(reinterpret_cast<char *>(hash), SHA_DIGEST_LENGTH);

    std::vector<std::string> peers = get_peers(announce_url, info_hash, length);

    std::string peer = peers[0];

    size_t colon_pos = peer.find(":");
    if (colon_pos == std::string::npos) {
      std::cerr << "Invalid peer address: " << peer << std::endl;

      return 1;
    }

    std::string peer_ip = peer.substr(0, colon_pos);
    std::string peer_port_str = peer.substr(colon_pos + 1);

    uint16_t peer_port = std::stoi(peer_port_str);

    std::vector<uint8_t> handshake = generate_handshake(info_hash, 0);

    int sock = socket(AF_INET, SOCK_STREAM, 0);

    sockaddr_in server;

    server.sin_family = AF_INET;
    server.sin_port = htons(peer_port);
    server.sin_addr.s_addr = inet_addr(peer_ip.c_str());

    char buf[68];

    if (connect(sock, (struct sockaddr *)&server, sizeof(server)) == -1) {
      perror("connection failed");

      close(sock);

      return 1;
    }

    send(sock, handshake.data(), handshake.size(), 0);

    if (recv(sock, buf, sizeof(buf), 0) == -1) {
      perror("recv failed");

      close(sock);

      return 1;
    }

    static const char hex_chars[] = "0123456789abcdef";

    std::string received_peer_id;
    received_peer_id.reserve(40);

    for (size_t i = 48; i < 68; ++i) {
      unsigned char byte = static_cast<unsigned char>(buf[i]);

      received_peer_id.push_back(hex_chars[byte >> 4]);
      received_peer_id.push_back(hex_chars[byte & 0x0F]);
    }

    char buffer[1024];

    while (buffer[4] != 5) {
      if (recv(sock, buffer, sizeof(buffer), 0) == -1) {
        perror("recv failed");

        close(sock);

        return 1;
      }
    }

    char interested[5] = {0, 0, 0, 1, 2};

    send(sock, interested, sizeof(interested), 0);

    while (buffer[4] != 1) {
      if (recv(sock, buffer, sizeof(buffer), 0) == -1) {
        perror("recv failed");

        close(sock);

        return 1;
      }
    }

    int num_pieces =
        length / (int)piece_length + (length % (int)piece_length != 0);

    char downloaded_file[length];

    for (int piece_index = 0; piece_index < num_pieces; ++piece_index) {
      int p_length = (int)piece_length;

      if (piece_index == num_pieces - 1 && (length % (int)piece_length != 0)) {
        p_length = length % (int)piece_length;
      }

      int num_blocks =
          p_length / (int)BLOCK_LENGTH + (p_length % (int)BLOCK_LENGTH != 0);

      char piece[p_length];

      for (int block_index = 0; block_index < num_blocks; ++block_index) {
        unsigned char request[17];

        uint32_t message_length = 12;

        request[0] = (message_length >> 24) & 0xFF;
        request[1] = (message_length >> 16) & 0xFF;
        request[2] = (message_length >> 8) & 0xFF;
        request[3] = message_length & 0xFF;
        request[4] = 6;

        request[5] = (piece_index >> 24) & 0xFF;
        request[6] = (piece_index >> 16) & 0xFF;
        request[7] = (piece_index >> 8) & 0xFF;
        request[8] = piece_index & 0xFF;

        uint32_t block_begin = block_index * BLOCK_LENGTH;

        request[9] = (block_begin >> 24) & 0xFF;
        request[10] = (block_begin >> 16) & 0xFF;
        request[11] = (block_begin >> 8) & 0xFF;
        request[12] = block_begin & 0xFF;

        uint32_t block_length = BLOCK_LENGTH;

        if (block_index == num_blocks - 1 && (p_length % BLOCK_LENGTH != 0)) {
          block_length = p_length % BLOCK_LENGTH;
        }

        request[13] = (block_length >> 24) & 0xFF;
        request[14] = (block_length >> 16) & 0xFF;
        request[15] = (block_length >> 8) & 0xFF;
        request[16] = block_length & 0xFF;

        send(sock, request, sizeof(request), 0);

        char response[13 + block_length];

        if (recv(sock, response, sizeof(response), MSG_WAITALL) == -1) {
          perror("recv failed");

          close(sock);

          return 1;
        }

        if (response[4] != 7) {
          perror("invalid message id, expected 7");

          close(sock);

          return 1;
        }

        memcpy(piece + block_index * BLOCK_LENGTH, response + 13, block_length);
      }

      unsigned char piece_hash[SHA_DIGEST_LENGTH];
      SHA1(reinterpret_cast<const unsigned char *>(piece), p_length,
           piece_hash);

      std::string p_hash(reinterpret_cast<char *>(piece_hash),
                         SHA_DIGEST_LENGTH);

      std::string expected_hash(piece_hashes.begin() + piece_index * 20,
                                piece_hashes.begin() + (piece_index + 1) * 20);

      if (expected_hash != p_hash) {
        perror("Piece hash mismatch");

        fprintf(stderr, "Piece hash mismatch");

        fprintf(stderr, "Expected hash: %s\n", expected_hash.c_str());
        fprintf(stderr, "Received hash: %s\n", p_hash.c_str());

        return 1;
      }

      memcpy(downloaded_file + piece_index * (int)piece_length, piece,
             p_length);
    }

    std::ofstream outFile(output_file, std::ios::binary);

    if (outFile.is_open()) {
      outFile.write(downloaded_file, length);
      outFile.close();
    }

    close(sock);
  }

  return 0;
}
