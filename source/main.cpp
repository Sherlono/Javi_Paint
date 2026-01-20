#include "bn_core.h"
#include "bn_log.h"
#include "bn_math.h"
#include "bn_deque.h"
#include "bn_keypad.h"
#include "bn_string.h"
#include "bn_display.h"
#include "bn_sprite_ptr.h"
#include "bn_bg_palettes.h"
#include "bn_sprite_tiles_ptr.h"
#include "bn_sprite_text_generator.h"
#include "bn_sprite_palette_actions.h"
#include "bn_sp_direct_bitmap_bg_painter.h"
#include "common_variable_8x8_sprite_font.h"

#include "bn_sprite_items_poi.h"
#include "bn_sprite_items_cursor.h"
#include "bn_sprite_items_color_display.h"

namespace jv
{
    [[nodiscard]] bn::fixed_point Lerp(bn::fixed_point a, bn::fixed_point b, bn::fixed t){
        return a + (b-a) * t;
    }

    const uint8_t WIDTH = bn::display::width(), HEIGHT = bn::display::height();

    struct FFData{
        ~FFData() = default;
        template <typename numType1, typename numType2, typename numType3>
        FFData(numType1 _x1, numType2 _x2, numType3 _y, bool _dy): x1(_x1), x2(_x2), y(_y), dy(_dy){}
        [[nodiscard]] int Dy(){
             if(dy) return 1;
             else return 0;
        }
        unsigned char x1 = 0, x2 = 0, y = 0;
        bool dy = false;
    };

    class Cursor{
    public:
        ~Cursor() = default;
        Cursor(bn::sp_direct_bitmap_bg_ptr bg): _cursor(bn::sprite_items::cursor.create_sprite(0, 0)),
                                                _painter(bg),
                                                _pixels(bg.page())
            {
                _painter.fill(bn::color(16, 16, 16));
            }

        void update(){
            Move();
            if (bn::keypad::a_pressed()) Pressed();
            if (bn::keypad::a_held()) Held();
            if (bn::keypad::a_released()) Released();
            
            if (bn::keypad::b_pressed()) Color_Mode();

            if (bn::keypad::l_pressed()) Set_Mode(_mode == 0 ? Mode::End - 1 : (_mode - 1) % Mode::End);
            if (bn::keypad::r_pressed()) Set_Mode((_mode + 1) % Mode::End);

            if (bn::keypad::start_pressed()) _painter.fill(_mainColor);
            if (bn::keypad::select_pressed()) Swap_Color();
        }

    private:
        enum Mode {Brush, Eraser, Line, Square, Circle, Bucket, Picker, End};

        const int X_OFFSET = (WIDTH/2) - 2, Y_OFFSET = (HEIGHT/2);
        const bn::fixed L_TIME = 0.05f;

        [[nodiscard]] bn::point Tip_Position() const { return bn::point(_cursor.x().integer() + X_OFFSET, _cursor.y().integer() + Y_OFFSET); }
        
        [[nodiscard]] bool Inside(int x, int y){
            return x >= 0 && y >= 0 && x < WIDTH && y < HEIGHT;
        }

        [[nodiscard]] bn::color Pixel(int x, int y){
            return _pixels[x + y * WIDTH];
        }

        void FloodFill(bn::point tipPos) {
            if (Pixel(tipPos.x(), tipPos.y()) == _mainColor) return;

            const bn::color oldColor = Pixel(tipPos.x(), tipPos.y());

            bn::deque<FFData, 128> points;
            points.push_back(FFData(tipPos.x(), tipPos.x(), tipPos.y(), true));
            points.push_back(FFData(tipPos.x(), tipPos.x(), tipPos.y() - 1, false));

            while (points.size() > 0){
                FFData p = points.front();
                points.pop_front();
                int x = p.x1;
                if (Inside(x, p.y) && Pixel(x, p.y) == oldColor){
                    while(Inside(x - 1, p.y) && Pixel(x - 1, p.y) == oldColor){
                        _painter.plot(x - 1, p.y, _mainColor);
                        x = x - 1;
                    }
                    if(x < p.x1){
                        points.push_back(FFData(x, p.x1 - 1, p.y - p.Dy(), !p.dy));
                    }
                }
                while(p.x1 <= p.x2){
                    while(Inside(p.x1, p.y) && Pixel(p.x1, p.y) == oldColor){
                        _painter.plot(p.x1, p.y, _mainColor);
                        p.x1 = p.x1 + 1;
                    }
                    if(p.x1 > x){
                        points.push_back(FFData(x, p.x1 - 1, p.y + p.Dy(), p.dy));
                    }
                    if(p.x1 - 1 > p.x2){
                        points.push_back(FFData(p.x2 + 1, p.x1 - 1, p.y - p.Dy(), !p.dy));
                    }
                    p.x1 = p.x1 + 1;
                    while(p.x1 <= p.x2 && Inside(p.x1, p.y) && Pixel(p.x1, p.y) != oldColor){
                        p.x1 = p.x1 + 1;
                    }
                    x = p.x1;
                }
            }
        }

        void Move(){
            if(bn::keypad::up_held()){              // Up
                if(_cursor.y() + Y_OFFSET - 1 >= 0) _cursor.set_position(_cursor.x(), _cursor.y() - 1);
            }else if(bn::keypad::down_held()){      // Down
                if(_cursor.y() + Y_OFFSET + 1 < HEIGHT) _cursor.set_position(_cursor.x(), _cursor.y() + 1);
            }
            if(bn::keypad::left_held()){            // Left
                if(_cursor.x() + X_OFFSET - 1 >= 0) _cursor.set_position(_cursor.x() - 1, _cursor.y());
            }else if(bn::keypad::right_held()){     // Right
                if(_cursor.x() + X_OFFSET + 1 < WIDTH) _cursor.set_position(_cursor.x() + 1, _cursor.y());
            }
        }

        void Pressed(){
            switch(_mode){
                case Mode::Brush: {
                    if(!bn::keypad::b_held()){
                        _brushTarget = _cursor.position();
                        _painter.plot(Tip_Position(), _mainColor);
                    }
                    break;
                }
                case Mode::Eraser: {
                    _brushTarget = _cursor.position();
                    for(int i = 0; i < 5; i++) _painter.line(Tip_Position() + bn::point(-3, i - 3), Tip_Position() + bn::point(2, i - 3), _backupColor);
                    break;
                }
                case Mode::Line: {
                    _startPoint = Tip_Position();
                    _painter.plot(_startPoint, _mainColor);

                    while (bn::keypad::a_held()){
                        if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
                        Move();
                        bn::core::update();
                    }
                    break;
                }
                case Mode::Square: {
                    _startPoint = Tip_Position();
                    _tempSprite_1 = bn::sprite_items::poi.create_sprite(_startPoint - bn::point(WIDTH, HEIGHT)/2);

                    while (bn::keypad::a_held()){
                        if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
                        Move();
                        bn::core::update();
                    }
                    break;
                }
                case Mode::Circle: {
                    _startPoint = Tip_Position();
                    _tempSprite_1 = bn::sprite_items::poi.create_sprite(_startPoint - bn::point(WIDTH, HEIGHT)/2);

                    while (bn::keypad::a_held()){
                        if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
                        Move();
                        bn::core::update();
                    }
                    break;
                }
                case Mode::Bucket: {
                    FloodFill(Tip_Position());
                    bn::core::update();
                    break;
                }
                case Mode::Picker: {
                    bn::point tip_position = Tip_Position();
                    _mainColor = _pixels[tip_position.x() + tip_position.y() * WIDTH];
                    break;
                }
                default:
                    break;
            }
        }
        
        void Held(){
            switch(_mode){
                case Mode::Brush: {
                    if(!bn::keypad::b_held()){
                        _brushTarget = Lerp(_brushTarget, _cursor.position(), L_TIME);
                        _painter.plot((_brushTarget.x() + X_OFFSET).integer(), (_brushTarget.y() + Y_OFFSET).integer(), _mainColor);
                    }
                    break;
                }
                case Mode::Eraser: {
                    _brushTarget = Lerp(_brushTarget, _cursor.position(), L_TIME);
                    for(int i = 0; i < 5; i++) _painter.line(Tip_Position() + bn::point(-3, i - 3), Tip_Position() + bn::point(2, i - 3), _backupColor);
                    break;
                }
                default:
                    break;
            }
        }
        
        void Released(){
            switch(_mode){
                case Mode::Line: {
                    _painter.line(_startPoint, Tip_Position(), _mainColor);
                    break;
                }
                case Mode::Square: {
                    _tempSprite_1.reset();
                    const bn::point endPoint = Tip_Position();

                    const int x_start = bn::min(_startPoint.x(), endPoint.x()), x_end = bn::max(_startPoint.x(), endPoint.x());
                    const int y_start = bn::min(_startPoint.y(), endPoint.y()), y_end = bn::max(_startPoint.y(), endPoint.y());

                    for (int i = 0; i < y_end - y_start + 1; i++){
                        bn::point lineStart(x_start, y_start + i);
                        bn::point lineEnd(x_end, y_start + i);
                        _painter.line(lineStart, lineEnd, _mainColor);
                    }
                    break;
                }
                case Mode::Circle: {
                    _tempSprite_1.reset();
                    const bn::point endPoint = Tip_Position();
                    
                    const int aux_x = _startPoint.x() - endPoint.x(), aux_y = _startPoint.y() - endPoint.y();
                    const int r = bn::sqrt(aux_x*aux_x + aux_y*aux_y);
                    bn::fixed d = 1.25 - r;
                    int x = 0, y = r;

                    while (x <= y)
                    {
                        if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
                        _painter.plot(_startPoint.x() + x, _startPoint.y() + y, _mainColor);
                        _painter.plot(_startPoint.x() - x, _startPoint.y() + y, _mainColor);
                        _painter.plot(_startPoint.x() + x, _startPoint.y() - y, _mainColor);
                        _painter.plot(_startPoint.x() - x, _startPoint.y() - y, _mainColor);
                        _painter.plot(_startPoint.x() + y, _startPoint.y() + x, _mainColor);
                        _painter.plot(_startPoint.x() - y, _startPoint.y() + x, _mainColor);
                        _painter.plot(_startPoint.x() + y, _startPoint.y() - x, _mainColor);
                        _painter.plot(_startPoint.x() - y, _startPoint.y() - x, _mainColor);
                        x++;
                        if (d < 0) d += 2 * x + 3;
                        else
                        {
                            y--;
                            d += 2 * (x - y) + 5;
                        }
                    }
                    break;
                }
                default:
                    break;
            }
        }

        void Set_Mode(int mode){
            _mode = mode;
            _cursor.set_tiles(bn::sprite_items::cursor.tiles_item().create_tiles(_mode));
        }
        
        void Swap_Color(){
            bn::color temp = _mainColor;
            _mainColor = _backupColor;
            _backupColor = temp;
        }

        void Color_Mode(){
            int colorIndex = 0;
            bn::fixed rgb[3] = {_mainColor.red(), _mainColor.green(), _mainColor.blue()};
            const char rgbChar[3] = {'R', 'G', 'B'};

            bn::sprite_text_generator text_generator(common::variable_8x8_sprite_font);
            bn::vector<bn::sprite_ptr, 3> txt_sprts;
            bn::string<3> text = "";
            
            _tempSprite_1 = bn::sprite_items::color_display.create_sprite(Tip_Position() - bn::point(WIDTH/2 - 18, HEIGHT/2 - 10));
            _tempSprite_1.value().set_bg_priority(0);
            _tempSprite_2 = bn::sprite_items::color_display.create_sprite(Tip_Position() - bn::point(WIDTH/2 - 20, HEIGHT/2 - 12));
            _tempSprite_2.value().set_palette(bn::sprite_palette_ptr::create_new(bn::sprite_items::color_display.palette_item()));
            
            bn::sprite_palette_ptr color_palettes[2] = {_tempSprite_1.value().palette(), _tempSprite_2.value().palette()};

            while(!bn::keypad::a_released()){
                if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
                if (bn::keypad::select_pressed()){
                    _mainColor.set_components(rgb[0].integer(), rgb[1].integer(), rgb[2].integer());
                    rgb[0] = _backupColor.red();
                    rgb[1] = _backupColor.green();
                    rgb[2] = _backupColor.blue();
                    Swap_Color();
                    colorIndex = 0;
                }
                
                txt_sprts.clear();
                text = "";

                if (bn::keypad::left_pressed() && colorIndex > 0) colorIndex--;
                else if (bn::keypad::right_pressed() && colorIndex < 2) colorIndex++;

                if (rgb[colorIndex] < 31){
                    if (bn::keypad::up_pressed()) rgb[colorIndex] += 1;
                    else if (bn::keypad::up_held()) rgb[colorIndex] += bn::fixed(0.1);
                }
                if (rgb[colorIndex] > 0){
                    if (bn::keypad::down_pressed()) rgb[colorIndex] -= 1;
                    else if (bn::keypad::down_held()) rgb[colorIndex] -= bn::fixed(0.1);
                }
                if(bn::keypad::l_pressed()) rgb[colorIndex] - 10 >= 0 ? rgb[colorIndex] -= 10 : rgb[colorIndex] = 0;
                if(bn::keypad::r_pressed()) rgb[colorIndex] + 10 <= 31 ? rgb[colorIndex] += 10 : rgb[colorIndex] = 31;

                color_palettes[0].set_fade(bn::color(rgb[0].integer(), rgb[1].integer(), rgb[2].integer()), 1);
                color_palettes[1].set_fade(_backupColor, 1);

                text = text + rgbChar[colorIndex] + bn::to_string<3>(rgb[colorIndex].integer());
                text_generator.generate(_cursor.x() + 8, _cursor.y(), text, txt_sprts);

                bn::core::update();
            }

            _mainColor.set_components(rgb[0].integer(), rgb[1].integer(), rgb[2].integer());
            txt_sprts.clear();
            
            _tempSprite_1.reset();
            _tempSprite_2.reset();
            bn::core::update();
        }

        bn::sprite_ptr _cursor;
        bn::optional<bn::sprite_ptr> _tempSprite_1, _tempSprite_2;
        bn::sp_direct_bitmap_bg_painter _painter;
        bn::span<bn::color> _pixels;
        bn::color _mainColor = bn::color(0, 0, 0), _backupColor = bn::color(16, 16, 16);
        bn::fixed_point _brushTarget;
        bn::point _startPoint;
        int8_t _mode = Mode::Brush;
    };
    
}

int main()
{
    bn::core::init();

    bn::sp_direct_bitmap_bg_ptr bg = bn::sp_direct_bitmap_bg_ptr::create();
    jv::Cursor cursor(bg);
    
    bn::bg_palettes::set_transparent_color(bn::color(16, 16, 16));

    while(true)
    {
        cursor.update();
        if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
        bn::core::update();
    }
}
