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

#include "bn_type_traits.h"

#include "bn_sprite_items_poi.h"
#include "bn_sprite_items_cursor.h"
#include "bn_sprite_items_color_display.h"

namespace jv
{
    [[nodiscard]] bn::fixed_point Lerp(bn::fixed_point a, bn::fixed_point b, bn::fixed t){
        return a + (b-a) * t;
    }

    struct SmallPoint{
        ~SmallPoint() = default;
        SmallPoint(){}
        template <typename numTypeA, typename numTypeB>
        SmallPoint(numTypeA _x, numTypeB _y): x(_x), y(_y) {}
        SmallPoint(bn::point point): x(point.x()), y(point.y()) {}
        
        SmallPoint& operator=(bn::point other){
            x = other.x();
            y = other.y();
            return *this;
        }
        SmallPoint& operator=(const bn::point& other){
            x = other.x();
            y = other.y();
            return *this;
        }
        SmallPoint& operator=(bn::point&& other){
            x = other.x();
            y = other.y();
            return *this;
        }

        unsigned char x = 0, y = 0;
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

        [[nodiscard]] bn::point Tip_Position() const { return bn::point(_cursor.x().integer() + X_OFFSET, _cursor.y().integer() + Y_OFFSET); }
        
        void FloodFill() {
            const bn::point tipPos = Tip_Position();
            const uint8_t width = bn::display::width(), height = bn::display::height();
            
            int pixelIndex = tipPos.x() + tipPos.y() * width;
            if (_pixels[pixelIndex] == _mainColor) return;

            const bn::color oldColor = _pixels[pixelIndex];

            bn::deque<SmallPoint, 1024> points;
            points.push_back(SmallPoint(tipPos));

            while (points.size() > 0){
                if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
                const SmallPoint p = points.front();
                points.pop_front();

                const uint8_t x_plus_1 = p.x + 1, y_plus_1 = p.y + 1;
                if (x_plus_1 > width || y_plus_1 > height) continue;

                const int y_times_w = p.y * width;
                pixelIndex = p.x + y_times_w;

                if (_pixels[pixelIndex] == oldColor){
                    _painter.plot(p.x, p.y, _mainColor);
                    const int x_minus_1 = p.x - 1, y_minus_1 = p.y - 1;
                    if (x_plus_1 < width && _pixels[x_plus_1 + y_times_w] != _mainColor){
                        points.push_back(SmallPoint(x_plus_1, p.y));
                    }
                    if (x_minus_1 >= 0 && _pixels[x_minus_1 + y_times_w] != _mainColor){
                        points.push_back(SmallPoint(x_minus_1, p.y));
                    }
                    if (y_plus_1 < height && _pixels[p.x + y_plus_1 * width] != _mainColor){
                        points.push_back(SmallPoint(p.x, y_plus_1));
                    }
                    if (y_minus_1 >= 0 && _pixels[p.x + y_minus_1 * width] != _mainColor){
                        points.push_back(SmallPoint(p.x, y_minus_1));
                    }
                }
            }
        }



        void Move(){
            if(bn::keypad::up_held()){              // Up
                if(_cursor.y() + Y_OFFSET - 1 >= 0) _cursor.set_position(_cursor.x(), _cursor.y() - 1);
            }else if(bn::keypad::down_held()){      // Down
                if(_cursor.y() + Y_OFFSET + 1 < bn::display::height()) _cursor.set_position(_cursor.x(), _cursor.y() + 1);
            }
            if(bn::keypad::left_held()){            // Left
                if(_cursor.x() + X_OFFSET - 1 >= 0) _cursor.set_position(_cursor.x() - 1, _cursor.y());
            }else if(bn::keypad::right_held()){     // Right
                if(_cursor.x() + X_OFFSET + 1 < bn::display::width()) _cursor.set_position(_cursor.x() + 1, _cursor.y());
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
                    _tempSprite = bn::sprite_items::poi.create_sprite(_startPoint - bn::point(bn::display::width(), bn::display::height())/2);

                    while (bn::keypad::a_held()){
                        if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
                        Move();
                        bn::core::update();
                    }
                    break;
                }
                case Mode::Circle: {
                    _startPoint = Tip_Position();
                    _tempSprite = bn::sprite_items::poi.create_sprite(_startPoint - bn::point(bn::display::width(), bn::display::height())/2);

                    while (bn::keypad::a_held()){
                        if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
                        Move();
                        bn::core::update();
                    }
                    break;
                }
                case Mode::Bucket: {
                    FloodFill();
                    bn::core::update();
                    break;
                }
                case Mode::Picker: {
                    bn::point tip_position = Tip_Position();
                    _mainColor = _pixels[tip_position.x() + tip_position.y() * bn::display::width()];
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
                    _tempSprite.reset();
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
                    _tempSprite.reset();
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
            
            _tempSprite = bn::sprite_items::color_display.create_sprite(Tip_Position() - bn::point(-18 + bn::display::width()/2, -10 + bn::display::height()/2));
            bn::sprite_palette_ptr color_palette = _tempSprite.value().palette();

            while(!bn::keypad::a_released()){
                if(bn::keypad::a_held() && bn::keypad::b_held() && bn::keypad::start_held() && bn::keypad::select_held()) bn::core::reset();
                txt_sprts.clear();
                bn::string<3> text = "";
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

                color_palette.set_fade(bn::color(rgb[0].integer(), rgb[1].integer(), rgb[2].integer()), 1);

                text = text + rgbChar[colorIndex] + bn::to_string<3>(rgb[colorIndex].integer());
                text_generator.generate(_cursor.x() + 8, _cursor.y(), text, txt_sprts);

                bn::core::update();
            }
            _mainColor.set_components(rgb[0].integer(), rgb[1].integer(), rgb[2].integer());
            txt_sprts.clear();
            
            _tempSprite.reset();
            bn::core::update();
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

        const int X_OFFSET = (bn::display::width()/2) - 2, Y_OFFSET = (bn::display::height()/2);
        const bn::fixed L_TIME = 0.05f;

        bn::sprite_ptr _cursor;
        bn::optional<bn::sprite_ptr> _tempSprite;
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
