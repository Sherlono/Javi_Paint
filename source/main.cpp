#include "bn_core.h"
#include "bn_log.h"
#include "bn_deque.h"
#include "bn_keypad.h"
#include "bn_string.h"
#include "bn_display.h"
#include "bn_sprite_ptr.h"
#include "bn_bg_palettes.h"
#include "bn_sprite_tiles_ptr.h"
#include "bn_sprite_text_generator.h"
#include "bn_sp_direct_bitmap_bg_painter.h"
#include "common_variable_8x8_sprite_font.h"

#include "bn_type_traits.h"

#include "bn_sprite_items_cursor.h"

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
        Cursor(bn::sp_direct_bitmap_bg_ptr bg): _sprite(bn::sprite_items::cursor.create_sprite(0, 0)),
                                                _painter(bg),
                                                _pixels(bg.page())
            {
                _painter.fill(bn::color(16, 16, 16));
            }

        [[nodiscard]] bn::point Tip_Position() const { return bn::point(_sprite.x().integer() + X_OFFSET, _sprite.y().integer() + Y_OFFSET); }
        
        void FloodFill() {
            const bn::point tipPos = Tip_Position();
            const uint8_t width = bn::display::width(), height = bn::display::height();
            
            int pixelIndex = tipPos.x() + tipPos.y() * width;
            if (_pixels[pixelIndex] == _color) return;

            const bn::color oldColor = _pixels[pixelIndex];

            bn::deque<SmallPoint, 1024> points;
            points.push_back(SmallPoint(tipPos));

            while (points.size() > 0){
                const SmallPoint p = points.front();
                points.pop_front();

                const uint8_t x_plus_1 = p.x + 1, y_plus_1 = p.y + 1;
                if (x_plus_1 > width || y_plus_1 > height) continue;

                const int y_times_w = p.y * width;
                pixelIndex = p.x + y_times_w;

                if (_pixels[pixelIndex] == oldColor){
                    _painter.plot(p.x, p.y, _color);
                    const int x_minus_1 = p.x - 1, y_minus_1 = p.y - 1;
                    if (x_plus_1 < width && _pixels[x_plus_1 + y_times_w] != _color){
                        points.push_back(SmallPoint(x_plus_1, p.y));
                    }
                    if (x_minus_1 >= 0 && _pixels[x_minus_1 + y_times_w] != _color){
                        points.push_back(SmallPoint(x_minus_1, p.y));
                    }
                    if (y_plus_1 < height && _pixels[p.x + y_plus_1 * width] != _color){
                        points.push_back(SmallPoint(p.x, y_plus_1));
                    }
                    if (y_minus_1 >= 0 && _pixels[p.x + y_minus_1 * width] != _color){
                        points.push_back(SmallPoint(p.x, y_minus_1));
                    }
                }
            }
        }

        void Move(){
            if(bn::keypad::up_held()){              // Up
                if(_sprite.y() + Y_OFFSET - 1 >= 0) _sprite.set_position(_sprite.x(), _sprite.y() - 1);
            }else if(bn::keypad::down_held()){      // Down
                if(_sprite.y() + Y_OFFSET + 1 < bn::display::height()) _sprite.set_position(_sprite.x(), _sprite.y() + 1);
            }
            if(bn::keypad::left_held()){            // Left
                if(_sprite.x() + X_OFFSET - 1 >= 0) _sprite.set_position(_sprite.x() - 1, _sprite.y());
            }else if(bn::keypad::right_held()){     // Right
                if(_sprite.x() + X_OFFSET + 1 < bn::display::width()) _sprite.set_position(_sprite.x() + 1, _sprite.y());
            }
        }

        void Pressed(){
            switch(_mode){
                case Mode::Brush: {
                    _brushTarget = _sprite.position();
                    _painter.plot(Tip_Position(), _color);
                    break;
                }
                case Mode::Line: {
                    _startPoint = Tip_Position();
                    _painter.plot(_startPoint, _color);

                    while (bn::keypad::a_held()){
                        Move();
                        bn::core::update();
                    }
                    break;
                }
                case Mode::Square: {
                    _startPoint = Tip_Position();
                    _painter.plot(_startPoint, _color);

                    while (bn::keypad::a_held()){
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
                    _color = _pixels[tip_position.x() + tip_position.y() * bn::display::width()];
                    break;
                }
                default:
                    break;
            }
        }
        
        void Held(){
            switch(_mode){
                case Mode::Brush: {
                    _brushTarget = Lerp(_brushTarget, _sprite.position(), L_TIME);
                    _painter.plot((_brushTarget.x() + X_OFFSET).integer(), (_brushTarget.y() + Y_OFFSET).integer(), _color);
                    break;
                }
                default:
                    break;
            }
        }
        
        void Released(){
            switch(_mode){
                case Mode::Line: {
                    _painter.line(_startPoint, Tip_Position(), _color);
                    break;
                }
                case Mode::Square: {
                    const bn::point endPoint = Tip_Position();

                    const int x_start = bn::min(_startPoint.x(),endPoint.x()), x_end = bn::max(_startPoint.x(),endPoint.x());
                    const int y_start = bn::min(_startPoint.y(),endPoint.y()), y_end = bn::max(_startPoint.y(),endPoint.y());

                    for (int i = 0; i < y_end - y_start + 1; i++){
                        bn::point lineStart(x_start, y_start + i);
                        bn::point lineEnd(x_end, y_start + i);
                        _painter.line(lineStart, lineEnd, _color);
                    }
                    break;
                }
                default:
                    break;
            }
        }

        void Next_Mode(){
            _mode = (_mode + 1) % Mode::End;
            _sprite.set_tiles(bn::sprite_items::cursor.tiles_item().create_tiles(_mode));
        }

        void Color_Mode(){
            int colorIndex = 0;
            bn::fixed rgb[3] = {_color.red(), _color.green(), _color.blue()};
            const char rgbChar[3] = {'R', 'G', 'B'};

            bn::sprite_text_generator text_generator(common::variable_8x8_sprite_font);
            bn::vector<bn::sprite_ptr, 3> txt_sprts;
            
            while(!bn::keypad::a_released()){
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

                text = text + rgbChar[colorIndex] + bn::to_string<3>(rgb[colorIndex].integer());
                text_generator.generate(_sprite.x() + 8, _sprite.y(), text, txt_sprts);

                _color.set_components(rgb[0].integer(), rgb[1].integer(), rgb[2].integer());
                bn::core::update();
            }
            txt_sprts.clear();
            bn::core::update();
        }

        void update(){
            Move();
            if (bn::keypad::a_pressed()) Pressed();
            if (bn::keypad::a_held()) Held();
            if (bn::keypad::a_released()) Released();
            
            if (bn::keypad::l_pressed()) Next_Mode();
            if (bn::keypad::r_pressed()) Color_Mode();
            if (bn::keypad::start_pressed()) _painter.fill(_color);
        }

    private:
        enum Mode {Brush, Line, Square, Bucket, Picker, End};

        const int X_OFFSET = bn::display::width()/2, Y_OFFSET = bn::display::height()/2;
        const bn::fixed L_TIME = 0.05f;

        bn::sprite_ptr _sprite;
        bn::sp_direct_bitmap_bg_painter _painter;
        bn::span<bn::color> _pixels;
        bn::color _color = bn::color(0, 0, 0);
        bn::fixed_point _brushTarget;
        bn::point _startPoint;
        int _mode = Mode::Brush;
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
        bn::core::update();
    }
}
