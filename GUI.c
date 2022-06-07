//
// Created by biist on 06/05/2022.
//

#include "GUI.h"

void GetFileNameFromFileChooser(GtkFileChooser* file_chooser, gpointer user_data)
{
    gchar *filename = gtk_file_chooser_get_filename (file_chooser);
    struct UserData_s* data = (struct UserData_s*)user_data;
    if (!filename) return;

    // Get the file name of the svg
    data->filename = filename;
    // Load svg into a pixbuffer
    //data->svgpixbuf = gdk_pixbuf_new_from_file_at_scale(filename, 800, 800, TRUE, NULL);
    data->svgpixbuf = gdk_pixbuf_new_from_file(filename, NULL);
    //gdk_pixbuf_get_width(data->svgpixbuf);
    data->width =  gdk_pixbuf_get_width(data->svgpixbuf);
    data->height =  gdk_pixbuf_get_height(data->svgpixbuf);

    data->svgpixbuf = gdk_pixbuf_scale_simple(data->svgpixbuf, 800, 800, GDK_INTERP_HYPER);
    //data->svgpixbuf = gdk_pixbuf_new_from_file (filename, NULL);
    //data->svgpixbuf = gdk_pixbuf_scale_simple(data->svgpixbuf, 800, 800, GDK_INTERP_BILINEAR);

    // Calculate the FFT of all the svg points
    // Load the svg file in memory
    xmlDocPtr svgfile = PARSER_LoadSVG(filename);
    // Parse the svg file and create SVG shapes with attributes of the file
    svgShapeStack *svg_shapes = PARSER_GetShapesFromSVG(svgfile);
    // Transform to mathematical represnetation from svg shapes
    ShapeAbstract *abstract_shapes = SHAPE_CreateAbstractFromSVG(svg_shapes);

    // Get the sets of points from the abstract shapes this operation frees the abstract shapes stack
    size_t nb_points = 0;
    ShapePoint *points = SHAPE_GetPointsFromAbstractShapes(abstract_shapes, 0.1f, &nb_points);
    kiss_fft_cpx* complex_array = GetComplexArrayFromPoints(points, nb_points);
    if(data->fft_array)
        free(data->fft_array);
    data->fft_array = GetFFTOfComplexArray(complex_array, nb_points);
    data->pa_data->frequencies_array = data->fft_array;
    data->nb_points = nb_points;

    SHAPE_FreePoints(data->points_list);
    data->points_list = NULL;
    data->actual_time = 0.f;
    data->previous_time = 0.f;

    gtk_widget_queue_draw(GTK_WIDGET(data->drawing_area));
}

void GetPrecisionFromScale(GtkScale* precision_scale, gpointer user_data)
{
    struct UserData_s* data = (struct UserData_s*)user_data;
    data->precision = (int)gtk_range_get_value(GTK_RANGE(precision_scale));
    SHAPE_FreePoints(data->points_list);
    data->points_list = NULL;
    data->turn = false;
    data->previous_time = data->actual_time;
}

void SVGCheckButton(GtkCheckButton* button, gpointer user_data)
{
    struct UserData_s* data = (struct UserData_s*)user_data;
    if (gtk_toggle_button_get_active(&button->toggle_button))
    {
        data->drawsvg = TRUE;
    } else
        data->drawsvg = FALSE;
}

gint ForceRenderUpdate(gpointer user_data)
{
    struct UserData_s* data = (struct UserData_s*)user_data;
    gtk_widget_queue_draw(GTK_WIDGET(data->drawing_area));

    return G_SOURCE_CONTINUE;

}

void DrawOnScreen(GtkDrawingArea* drawing_area, cairo_t* cr, gpointer user_data)
{
    struct UserData_s* data = (struct UserData_s*)user_data;
    if(data->drawsvg)
        DrawSVG(cr, user_data);
    if(data->fft_array)
        SHAPE_AddPoint(&data->points_list, DrawEpicycloides(cr, user_data));
    DrawPoints(cr, user_data);
}

void DrawSVG(cairo_t* cr, gpointer user_data)
{
    struct UserData_s* data = (struct UserData_s*)user_data;
    if(!data->svgpixbuf)
        return;

    gdk_cairo_set_source_pixbuf(cr, data->svgpixbuf, 0, 0);
    cairo_paint (cr);
}

void DrawPoints(cairo_t* cr, gpointer user_data)
{
    struct UserData_s* data = (struct UserData_s*)user_data;
    float x_scale = 800.f/(float)data->width, y_scale = 800.f/(float)data->height;
    ShapePoint* points = data->points_list;
    cairo_set_source_rgba(cr, 1.f, 0, 0, 1.f);
    cairo_set_line_width(cr, 2.f);
    while(points)
    {
        cairo_arc(cr, points->x*x_scale, y_scale*points->y, 1.f, 0, 2 * M_PI);
        cairo_fill(cr);

        if(points->np)
        {
            cairo_move_to(cr, points->x*x_scale, points->y*y_scale);
            cairo_line_to(cr, points->np->x*x_scale, points->np->y*y_scale);
            cairo_stroke(cr);
        }

        points = points->np;
    }
}

void TakeScreenshotOfDrawing(GtkButton* button, gpointer user_data)
{
    struct UserData_s *data = (struct UserData_s *) user_data;
    gint width, height;
    GtkWindow* window = gtk_widget_get_window(GTK_WIDGET(data->drawing_area));
    gtk_window_get_size(window, &width, &height);
    printf("%d %d\n", width, height);
    GdkPixbuf* pixbuf = gdk_pixbuf_get_from_window(GDK_WINDOW(data->drawing_area), 0, 0, width, height);
    gdk_pixbuf_save(pixbuf, "fourier.jpeg", "jpeg", NULL, "quality", "100", NULL);
}

ShapePoint* DrawEpicycloides(cairo_t* cr, gpointer user_data) {
    struct UserData_s *data = (struct UserData_s *) user_data;
    // Select a random circle to read frequency from
    size_t frequency_cirlce_idx = rand()%(size_t)(((data->precision/100.f)*data->nb_points));
    data->pa_data->idx = frequency_cirlce_idx;

    float amplitude, phase;
    size_t frequency;
    float x = 0, y = 0;
    static float prev_x, prev_y;
    float x_scale = 800.f/(float)data->width, y_scale = 800.f/(float)data->height;
    const float dt = M_PI * 2 / (float) data->nb_points;

    if (data->actual_time >= 2 * M_PI){
        data->turn = TRUE;
        data->actual_time = 0.f;
    }

    cairo_set_source_rgba(cr, 0, 0, 0, 0.2);
    for (size_t i = 0; i < (size_t)((data->precision/100.f)*data->nb_points); i++) {
        prev_x = x;
        prev_y = y;

        amplitude = (float)data->fft_array[i].amplitude;
        phase =  (float)data->fft_array[i].phase;
        frequency = data->fft_array[i].frequency;

        x += amplitude * cosf(frequency *  data->actual_time + phase);
        y += amplitude * sinf(frequency *  data->actual_time + phase);

        if(prev_x && prev_y) {
            cairo_move_to(cr, prev_x*x_scale, prev_y*y_scale);
            cairo_line_to(cr, x*x_scale, y*y_scale);
            cairo_stroke(cr);
        }
            if(i == frequency_cirlce_idx)
                cairo_set_source_rgba(cr, 1, 0, 0, 0.5);
            else
                cairo_set_source_rgba(cr, 0, 0, 0, 0.2);

            cairo_arc(cr, x*x_scale, y*y_scale, amplitude, 0, 2 * M_PI);
            cairo_stroke(cr);
     }

        data->actual_time +=dt;
        if(data->turn)
            data->previous_time -= dt;
        if(!data->turn || data->previous_time > 0.f)
            return SHAPE_CreatePoint(x, y);
        return NULL;
}