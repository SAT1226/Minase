Original: https://github.com/nsf/termbox

Add support east asian ambiguous character width.
Add new function.
SO_IMPORT void tb_put_cell_front(int x, int y, const struct tb_cell *cell);
SO_IMPORT void tb_change_cell_front(int x, int y, uint32_t ch, uint16_t fg, uint16_t bg);
SO_IMPORT void tb_use_wcwidth_cjk(int flg);
SO_IMPORT int tb_wcwidth(const wchar_t c);
