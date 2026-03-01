# Components 

- Raspberry Pi 5 with 16GB RAM
- 2x HackRF One SDR
- (Highly recommended) HackRF TCXO
- 2x antennas tuned for the chosen frequency
- (Optional for ADS-B) Omni-directional ADS-B antenna
- (Optional for ADS-B) RTL-sdr (RTL2832U & R820T2 are fine. No need for an expensive one)
- (Optional for ADS-B) Low Noise Amplifier (LNA)
- (Optional for ADS-B & Cooling fan) DC-to-DC Converter
- (Optional) Cooling fan (5v or 12v)
- USB Cables
- Soldering kit
- Patch cables

# Software Installation

- Install Ubuntu Server 25.10
- Update Ubuntu
- Upgrade Ubuntu
- Install Docker by [using the apt repository](https://docs.docker.com/engine/install/ubuntu/)
- Install HackRF firmware and tools
  ```bash
  sudo apt install hackrf*
  ```
- Install RTLSdr tools
  ```bash
  sudo apt install rtl-sdr*
  ```
- Install [Blah2](https://github.com/30hours/blah2)
- Make sure to place the correct config file for HackRF One in ```/opt/blah2/config/config.yml```. Another type of SDR device config could be there be default. All example config files reside in the same directory.





# Physical build

**Circuit Diagram**

<img width="717" height="369" alt="Circuit-Diagram" src="https://github.com/user-attachments/assets/c50fbe89-3d40-4739-a544-893498f0a7ee" />
