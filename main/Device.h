#pragma once

class Device {
    i2s_chan_handle_t _tx_chan;
    i2s_chan_handle_t _rx_chan;

public:
    void begin();

private:
    void read_task();
    void write_task();
};
