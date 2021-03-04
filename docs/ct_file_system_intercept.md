# Overview

The CT application allows certain parameters to be updated using SMP over BLE or SMP over serial console.

# Parameters

| Path                         | Description                                                        |
| ---------------------------- | ------------------------------------------------------------------ |
| /nv/aws_topic_prefix.txt     | The prefix used in generation of the AWS topic                     |
| /nv/ble_network_id.txt       | The network ID used in BLE advertisements (Contact Tracing Format) |
| /nv/mqtt/client_id.txt       | The Client ID used to connect to AWS.                              |
| /nv/mqtt/endpoint.txt        | The AWS endpoint.                                                  |
| /nv/mqtt/root_ca.pem.crt     | Root Certificate                                                   |
| /nv/mqtt/certificate.pem.crt | Client Certificate                                                 |
| /nv/mqtt/private.pem.key     | Private Key                                                        |
| /nv/mqtt/save_clear.txt      | Save (1) / Clear (0) Certificates                                  |
| /nv/aes_key.bin              | AES Key used for communications between gateway and CT devices     |

## Example of Setting Certificates

Details about mcumgr and generating certificates can be found [here](aws_iot.md).

```
.\mcumgr.exe fs upload ./clear.txt /nv/mqtt/save_clear.txt -c ct
.\mcumgr.exe fs upload ./AmazonRootCA1.pem /nv/mqtt/root_ca.pem.crt -c ct
.\mcumgr.exe fs upload ./eadb344c02-certificate.pem.crt /nv/mqtt/certificate.pem.crt -c ct
.\mcumgr.exe fs upload ./eadb344c02-private.pem.key /nv/mqtt/private.pem.key -c ct
.\mcumgr.exe fs upload ./test_endpoint.txt /nv/mqtt/endpoint.txt -c ct
.\mcumgr.exe fs upload ./topic_prefix.txt /nv/aws_topic_prefix.txt -c ct
.\mcumgr.exe fs upload ./save.txt /nv/mqtt/save_clear.txt -c ct
.\mcumgr.exe reset -c ct
```

The content of the file "clear.txt" is 0.
The content of the file "save.txt" is 1.
