#ifndef __shapes_H__
#define __shapes_H__

#define tri_base 200
#define tri_height 200

//Defining my Shapes and their attributes.
typedef struct my_circle_t {
    signed short x_pos;
    signed short y_pos;
    signed short radius;
    unsigned int color;
} my_circle_t;

typedef struct my_square_t{
    signed short x_pos;
    signed short y_pos;
    signed short width;
    signed short height;
    unsigned int color;
} my_square_t;

typedef struct my_triangle_t{
    coord_t* points;
    unsigned int color;
} my_triangle_t;

//creation of my triangle
my_triangle_t *create_tri(signed short x_pos, signed short y_pos, unsigned int color);

//creation of my circle
my_circle_t* create_circ(signed short x_pos, signed short y_pos, signed short radius, unsigned int color);

//creation of my square
my_square_t* create_box(signed short x_pos, signed short y_pos, signed short side, unsigned int color);


#endif