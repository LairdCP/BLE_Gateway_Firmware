# Cellular Network Information (Pinnacle 100/MG100 only)

There are multiple commands that can be issued from the shell to get cellular information.

## Site Survey Command

Enter the command in the shell.
```
hl survey
```

This instructs the modem to scan.  Each scan may return different results.  Be patient, the response can take minutes.

When successful, the response will be similar to this:
```
EARFCN: 5110
Cell Id: 175
RSRP: -94
RSRQ: -9
EARFCN: 5110
Cell Id: 6
RSRP: -102
RSRQ: -17
survey status: 0
```

A status of 0 indicates that the command succeeded.  Any other value is a failure.
This information can be shared with your cellular provider when troubleshooting connectivity issues.

## Operator

The operator of the network can be found by issuing a command at the shell.
```
uart:~$ hl cmd 1 at+cops?
```

The modem will respond with data formatted as follows.
```
<dbg> modem_hl7800.send_at_cmd: OUT: [at+cops?]
<dbg> modem_hl7800.hl7800_rx: HANDLE +COPS:  (len:12)
<inf> modem_hl7800: Operator: 0,0,"AT&T",7
```

The first parameter indicates that registration is handled by the mobile equipment (modem).
The second parameter indicates the format of the third parameter (0 = long alphanumeric, 1 = short alphanumeric, 2= numeric).
The third parameter indicates the name of the network.
The fourth parameter is 7 for E-UTRAN (LTE-M1) and 9 for E-UTRAN NB-S1 (NB-IoT) mode.

## International Mobile Subscriber Identity

Enter the following at the shell.
```
attr query imsi
```

A status of zero indicates success.  Any other value indicates failure.
```
query status: 0
<inf> attr: [219] imsi                          '310170833073696'
```
