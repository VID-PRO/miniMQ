#include "DmxOut.h"
#include "DmxOut.pio.h"

#if defined(ARDUINO_ARCH_MBED)
  #include <clocks.h>
  #include <irq.h>
#else
  #include "hardware/clocks.h"
  #include "hardware/irq.h"
#endif

DmxOut::return_code DmxOut::begin(uint pin, PIO pio) {
  if (!pio_can_add_program(pio, &dmx_tx_program))
    return ERR_INSUFFICIENT_PRGM_MEM;
  _prgm_offset = pio_add_program(pio, &dmx_tx_program);

  int sm = pio_claim_unused_sm(pio, false);
  if (sm == -1) return ERR_NO_SM_AVAILABLE;

  // Route the pin to PIO and make it an output.
  pio_gpio_init(pio, pin);
  pio_sm_set_pins_with_mask(pio, sm, 1u << pin, 1u << pin);
  pio_sm_set_pindirs_with_mask(pio, sm, 1u << pin, 1u << pin);

  pio_sm_config sm_conf = dmx_tx_program_get_default_config(_prgm_offset);
  sm_config_set_out_pins(&sm_conf, pin, 1);
  sm_config_set_sideset_pins(&sm_conf, pin);

  uint clk_div = clock_get_hz(clk_sys) / DMX_SM_FREQ;
  sm_config_set_clkdiv(&sm_conf, clk_div);

  pio_sm_init(pio, sm, _prgm_offset, &sm_conf);
  pio_sm_set_enabled(pio, sm, true);

  int dma = dma_claim_unused_channel(false);
  if (dma == -1) return ERR_NO_DMA_AVAILABLE;

  dma_channel_config dma_conf = dma_channel_get_default_config(dma);
  channel_config_set_transfer_data_size(&dma_conf, DMA_SIZE_8);
  channel_config_set_dreq(&dma_conf, pio_get_dreq(pio, sm, true));
  dma_channel_set_write_addr(dma, &pio->txf[sm], false);
  dma_channel_set_config(dma, &dma_conf, false);

  _pio = pio;
  _sm = sm;
  _pin = pin;
  _dma = dma;

  return SUCCESS;
}

void DmxOut::write(uint8_t *universe, uint length) {
  // Restart the SM so exactly one frame (BREAK + data) is produced, then
  // feed it from the buffer via DMA. Same model as the reference driver.
  pio_sm_set_enabled(_pio, _sm, false);
  pio_sm_restart(_pio, _sm);
  pio_sm_exec(_pio, _sm, pio_encode_jmp(_prgm_offset));
  pio_sm_set_enabled(_pio, _sm, true);
  dma_channel_transfer_from_buffer_now(_dma, universe, length);
}

bool DmxOut::busy() {
  if (dma_channel_is_busy(_dma)) return true;
  return !pio_sm_is_tx_fifo_empty(_pio, _sm);
}

void DmxOut::end() {
  pio_sm_set_enabled(_pio, _sm, false);
  pio_remove_program(_pio, &dmx_tx_program, _prgm_offset);
  pio_sm_unclaim(_pio, _sm);
  dma_channel_unclaim(_dma);
}