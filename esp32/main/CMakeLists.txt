idf_component_register(
    SRCS
        "main.c"
        "pn7150.c"
        "skpass_usb.c"
        "skpass_cdc.c"
        "reader_frame.c"
        "hid_wire.c"
        "aime_reader.c"
        "smartcard_probe.c"
        "advanced_protocol.c"
        "advanced_reader.c"
        "mifare_reader.c"
        "writer_protocol.c"
        "card_writer.c"
        "uf2_update.c"
        "rom_download.c"
        "card_emulation.c"
        "ce_protocol.c"
    INCLUDE_DIRS "."
    REQUIRES skpass
    PRIV_REQUIRES driver esp_timer esp_tinyusb app_update spi_flash esp_app_format json efuse
)
