async def to_code(config):
    var = cg.new_Pvariable(config[CONF_ID])
    await cg.register_component(var, config)
    await uart.register_uart_device(var, config)

    for blind_cfg in config.get(CONF_BLINDS, []):
        bid = blind_cfg[CONF_BLIND_ID]
        name = blind_cfg[CONF_NAME]

        # Create ARCBlind instance
        blind = cg.new_Pvariable(ARCBlind)
        await cg.register_component(blind, blind_cfg)
        await cover.register_cover(blind, blind_cfg)

        cg.add(blind.set_blind_id(bid))
        cg.add(blind.set_name(name))
        cg.add(var.add_blind(blind))

        # RF quality sensor
        lq = cg.new_Pvariable(sensor.Sensor)
        await sensor.register_sensor(lq, {"name": f"{name} RF Quality", "unit_of_measurement": "%"})
        cg.add(var.map_lq_sensor(bid, lq))

        # Status text sensor
        status = cg.new_Pvariable(text_sensor.TextSensor)
        await text_sensor.register_text_sensor(status, {"name": f"{name} Status"})
        cg.add(var.map_status_sensor(bid, status))
