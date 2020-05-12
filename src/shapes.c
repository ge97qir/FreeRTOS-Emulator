#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#include "TUM_Draw.h"

#include "shapes.h"


//REMEMBER: coord_t consists of two signed short variables (x and y)


//receives x_pos and y_pos of triangle's center position and creates a equilateral triangle.
my_triangle_t* create_tri(signed short x_pos, signed short y_pos, unsigned int color)
{
    my_triangle_t* tri=calloc(1,sizeof(my_triangle_t));

    if(!tri)
    {
        fprintf(stderr,"Creating Triangle Failed.\n");
        exit(EXIT_FAILURE);
    }

    //set left tip of triangle
    coord_t left_tip;

    left_tip.x=x_pos-tri_base/2;
    left_tip.y=y_pos+tri_height/2;
    
    //set right tip of triangle
    coord_t right_tip;
    right_tip.x=x_pos+tri_base/2;
    right_tip.y=y_pos+tri_height/2;

    //set upper tip of triangle
    coord_t upper_tip;
    upper_tip.x=x_pos;
    upper_tip.y=y_pos-tri_height/2;

    coord_t points[3]={right_tip,left_tip,upper_tip};
    
    tri->points=points;
    tri->color=color;

    return tri;
}

my_circle_t* create_circ(signed short x_pos, signed short y_pos, signed short radius, unsigned int color)
{
    my_circle_t* circ=calloc(1,sizeof(my_circle_t));

    if(!circ)
    {
        fprintf(stderr,"Creating Circle Failed.\n");
        exit(EXIT_FAILURE);
    }

    circ->x_pos=x_pos;
    circ->y_pos=y_pos;
    circ->color=color;
    circ->radius=radius;

    return circ;
}

my_square_t* create_box(signed short x_pos, signed short y_pos, signed short side, unsigned int color)
{
    my_square_t* box=calloc(1,sizeof(my_square_t));

    if(!box)
    {
        fprintf(stderr,"Creating Square Failed.\n");
        exit(EXIT_FAILURE);
    }
    
    box->x_pos=x_pos;
    box->y_pos=y_pos;
    box->width=side;
    box->height=side;
    box->color=color;

    return box;
}
