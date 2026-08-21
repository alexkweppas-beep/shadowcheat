#include "../headers/includes.h"

void c_widgets::init_keybinds()
{
	for (auto widget : menu_cfg->order)
	{
		if (widget.second == checkbox_type)
		{
			checkbox_t* data = menu_cfg->fill<checkbox_t>(widget.first);

			if (data && !data->keybinds_list.empty())
			{
				for (auto& kb : data->keybinds_list)
				{
                    if (kb.key == 0) continue;
                    static bool kb_toggled[4096] = { false };
                    int safe_idx = (kb.idx >= 0 && kb.idx < 4096) ? kb.idx : 0;
					if (kb.mode == 0)
					{
                        bool pressed = (GetAsyncKeyState(kb.key) & 0x8000) != 0;
                        if (pressed && !kb_toggled[safe_idx])
                        {
                            data->enabled = !data->enabled;
                            data->last_keybind = kb;
                            if (data->external_ptr) *data->external_ptr = data->enabled;
                        }
                        kb_toggled[safe_idx] = pressed;
					}

					if (kb.mode == 1)
					{
						if (GetAsyncKeyState(kb.key) & 0x8000)
						{
							data->enabled = true;
                            data->last_keybind = kb;
                            if (data->external_ptr) *data->external_ptr = data->enabled;
						}
						else
						{
							data->enabled = false;
                            data->last_keybind = kb;
                            if (data->external_ptr) *data->external_ptr = data->enabled;
						}
					}
				}
			}
		}
	}
}
