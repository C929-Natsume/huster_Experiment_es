#include <stdio.h>
#include "../common/common.h"

#define COLOR_BACKGROUND	FB_COLOR(0xff,0xff,0xff)

#define RED		FB_COLOR(255,0,0)
#define GREEN	FB_COLOR(0,255,0)
#define BLUE	FB_COLOR(0,0,255)
#define ORANGE	FB_COLOR(255,165,0)
#define PURPLE	FB_COLOR(139,0,255)

static int touch_fd;




static void touch_event_cb(int fd)
{
	int type,x,y,finger;
	int color;
	type = touch_read(fd, &x,&y,&finger);

    if (finger < 0 || finger >= MAX_FINGERS) {
        finger = 0;  // 默认使用第一个手指
    }
    
    // 清除按钮
    if (type == TOUCH_PRESS) {
        if (x >= CLEAR_BTN_X && x <= CLEAR_BTN_X + CLEAR_BTN_WIDTH &&
            y >= CLEAR_BTN_Y && y <= CLEAR_BTN_Y + CLEAR_BTN_HEIGHT) {
            clear_screen();
            fb_update();
            return;
        }
    }

	switch(type){
	case TOUCH_PRESS:
		printf("TOUCH_PRESS：x=%d,y=%d,finger=%d\n",x,y,finger);
        fingers[finger].is_active = 1;
        fingers[finger].last_x = x;
        fingers[finger].last_y = y;

		switch (finger)
		{
		case 0:
			color = RED;
			break;
		case 1:
			color = GREEN;
			break;
		case 2:
			color = BLUE;
			break;
		case 3:
			color = ORANGE;
			break;
		case 4:
			color = PURPLE;
			break;
		
		default:
			break;
		}
		fb_draw_thick_point(x, y, color, LINE_WIDTH/2);
		break;
	case TOUCH_MOVE:
        if (!fingers[finger].is_active) break;
		printf("TOUCH_MOVE：x=%d,y=%d,finger=%d\n",x,y,finger);
		switch (finger)
		{
		case 0:
			color = RED;
			break;
		case 1:
			color = GREEN;
			break;
		case 2:	
			color = BLUE;
			break;
		case 3:
			color = ORANGE;
			break;
		case 4:
			color = PURPLE;
			break;
		
		default:
			break;
		}

		int last_x = fingers[finger].last_x;
		int last_y = fingers[finger].last_y;
		fb_draw_thick_line(last_x, last_y, x, y, color, LINE_WIDTH);
		fingers[finger].last_x = x;
		fingers[finger].last_y = y;

		break;
	case TOUCH_RELEASE:
		printf("TOUCH_RELEASE：x=%d,y=%d,finger=%d\n",x,y,finger);
		fingers[finger].is_active = 0;
		fingers[finger].last_x = -1;
		fingers[finger].last_y = -1;
		break;
	case TOUCH_ERROR:
		printf("close touch fd\n");
		close(fd);
		task_delete_file(fd);
		break;
	default:
		return;
	}
	fb_update();
	return;
}

int main(int argc, char *argv[])
{
	fb_init("/dev/fb0");
	fb_draw_rect(0,0,SCREEN_WIDTH,SCREEN_HEIGHT,COLOR_BACKGROUND);
	fb_update();

	//打开多点触摸设备文件, 返回文件fd
	touch_fd = touch_init("/dev/input/event2");
	for(int i=0;i<MAX_FINGERS;i++){
		fingers[i].is_active = 0;
		fingers[i].last_x = -1;
		fingers[i].last_y = -1;
	}
	draw_clear_button();
	fb_update();
	//添加任务, 当touch_fd文件可读时, 会自动调用touch_event_cb函数
	task_add_file(touch_fd, touch_event_cb);
	
	task_loop(); //进入任务循环
	return 0;
}
