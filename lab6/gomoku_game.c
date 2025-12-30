#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <sys/socket.h>
#include "../common/common.h"
#include "audio_util.h"

#define COLOR_BACKGROUND	FB_COLOR(0xff,0xff,0xff)
#define COLOR_BOARD_LINE	FB_COLOR(0x0,0x0,0x0)
#define COLOR_BLACK_STONE	FB_COLOR(0x0,0x0,0x0)
#define COLOR_WHITE_STONE	FB_COLOR(0xff,0xff,0xff)
#define COLOR_TEXT			FB_COLOR(0x0,0x0,0x0)
#define COLOR_HIGHLIGHT		FB_COLOR(0xff,0x0,0x0)
#define COLOR_BUTTON_ACTIVE	FB_COLOR(0x90,0xee,0x90)  // 亮绿色
#define COLOR_BUTTON_INACTIVE FB_COLOR(0x80,0x80,0x80) // 灰色
#define COLOR_BUTTON_SELECTED FB_COLOR(0xff,0xd7,0x00) // 金黄色
#define COLOR_ENERGY_BAR	FB_COLOR(0x00,0xff,0x00)   // 绿色能量条

#define BOARD_SIZE 15
#define CELL_SIZE 35  // 增大格子
#define BOARD_MARGIN_X 30
#define BOARD_MARGIN_Y 30
#define BOARD_WIDTH (BOARD_SIZE - 1) * CELL_SIZE
#define BOARD_HEIGHT (BOARD_SIZE - 1) * CELL_SIZE
#define STONE_RADIUS 14
#define LINE_THICKNESS 3  // 加粗线条

// 技能类型
#define SKILL_NONE 0
#define SKILL_FLY_SAND 1      // 飞沙走石
#define SKILL_PICK_GOLD 2     // 拾金不昧
#define SKILL_CALM_WATER 3    // 静如止水
#define SKILL_TIME_REWIND 4   // 时光倒流
#define SKILL_POWER_UP 5      // 力拔山兮

// 右侧UI区域
#define UI_RIGHT_X 700
#define UI_RIGHT_Y 30
#define BUTTON_WIDTH 120
#define BUTTON_HEIGHT 50
#define BUTTON_SPACING 10
#define ENERGY_BAR_HEIGHT 8

// 语音识别
#define PCM_SAMPLE_RATE 16000
#define IP "127.0.0.1"
#define PORT 8011

// 游戏状态
#define GAME_STATE_WAITING_START 0  // 等待开始
#define GAME_STATE_PLAYING 1        // 游戏中
#define GAME_STATE_ENDED 2          // 游戏结束

// 棋盘状态：0=空，1=黑子，2=白子，3=能量果实
static int board[BOARD_SIZE][BOARD_SIZE];
static int current_player = 1; // 1=黑子，2=白子
static int game_state = GAME_STATE_WAITING_START;
static int game_over = 0;
static int winner = 0;
static int is_my_turn = 1;
static int my_role = 1;
static int role_initialized = 0;
static int my_ready = 0;      // 我是否准备好开始
static int opp_ready = 0;     // 对方是否准备好开始
static int my_restart = 0;    // 我是否点击重新开始
static int opp_restart = 0;   // 对方是否点击重新开始
static int touch_fd;
static int bluetooth_fd;

// 技能状态
static int skill_used[6] = {0}; // 0未使用，1已使用
static int energy_count = 0;    // 能量果实计数
static int energy_fruits[5][2]; // 能量果实位置
static int energy_fruits_placed = 0;

// 选中状态
static int selected_row = -1, selected_col = -1;
static int selected_skill = SKILL_NONE;
static int pick_gold_state = 0; // 0=未选择，1=已选己方，2=已选对方
static int pick_gold_my_row = -1, pick_gold_my_col = -1;
static int pick_gold_opp_row = -1, pick_gold_opp_col = -1;

// 历史记录（用于时光倒流）
#define MAX_HISTORY 100
static int history_count = 0;
static struct {
	int row, col, player;
	int was_energy_fruit; // 是否在能量果实位置
} history[MAX_HISTORY];

// 倒计时
static int time_left = 30; // 30秒
static int timer_period = 1000; // 1秒

// 缩放图片以适应屏幕大小
static fb_image *scale_image_to_screen(fb_image *src_img)
{
	if (src_img == NULL) return NULL;
	
	int src_w = src_img->pixel_w;
	int src_h = src_img->pixel_h;
	int dst_w = SCREEN_WIDTH;
	int dst_h = SCREEN_HEIGHT;
	
	// 如果图片已经和屏幕一样大或更大，直接返回原图（或裁剪）
	if (src_w == dst_w && src_h == dst_h) {
		return src_img;
	}
	
	// 创建缩放后的图片
	fb_image *dst_img = fb_new_image(src_img->color_type, dst_w, dst_h, 0);
	if (dst_img == NULL) return NULL;
	
	// 使用最近邻插值进行缩放
	if (src_img->color_type == FB_COLOR_RGB_8880 || src_img->color_type == FB_COLOR_RGBA_8888) {
		// RGB或RGBA格式
		int bytes_per_pixel = (src_img->color_type == FB_COLOR_RGB_8880) ? 4 : 4;
		
		for (int y = 0; y < dst_h; y++) {
			int src_y = (y * src_h) / dst_h;  // 最近邻插值
			if (src_y >= src_h) src_y = src_h - 1;
			
			for (int x = 0; x < dst_w; x++) {
				int src_x = (x * src_w) / dst_w;  // 最近邻插值
				if (src_x >= src_w) src_x = src_w - 1;
				
				// 计算源像素和目标像素的位置
				char *src_pixel = src_img->content + src_y * src_img->line_byte + src_x * bytes_per_pixel;
				char *dst_pixel = dst_img->content + y * dst_img->line_byte + x * bytes_per_pixel;
				
				// 复制像素
				memcpy(dst_pixel, src_pixel, bytes_per_pixel);
			}
		}
	} else if (src_img->color_type == FB_COLOR_ALPHA_8) {
		// Alpha格式
		for (int y = 0; y < dst_h; y++) {
			int src_y = (y * src_h) / dst_h;
			if (src_y >= src_h) src_y = src_h - 1;
			
			for (int x = 0; x < dst_w; x++) {
				int src_x = (x * src_w) / dst_w;
				if (src_x >= src_w) src_x = src_w - 1;
				
				char *src_pixel = src_img->content + src_y * src_img->line_byte + src_x;
				char *dst_pixel = dst_img->content + y * dst_img->line_byte + x;
				
				*dst_pixel = *src_pixel;
			}
		}
	}
	
	return dst_img;
}

// 将屏幕坐标转换为棋盘坐标（扩大判定范围）
static int screen_to_board(int x, int y, int *row, int *col)
{
	int board_x = x - BOARD_MARGIN_X;
	int board_y = y - BOARD_MARGIN_Y;
	
	if (board_x < -CELL_SIZE/2 || board_y < -CELL_SIZE/2) return 0;
	if (board_x > BOARD_WIDTH + CELL_SIZE/2 || board_y > BOARD_HEIGHT + CELL_SIZE/2) return 0;
	
	*col = (board_x + CELL_SIZE/2) / CELL_SIZE;
	*row = (board_y + CELL_SIZE/2) / CELL_SIZE;
	
	if (*row < 0 || *row >= BOARD_SIZE || *col < 0 || *col >= BOARD_SIZE) {
		return 0;
	}
	
	return 1;
}

// 检查是否在按钮区域内
static int check_button_click(int x, int y, int btn_x, int btn_y, int btn_w, int btn_h)
{
	return (x >= btn_x && x < btn_x + btn_w && y >= btn_y && y < btn_y + btn_h);
}

// 发送到语音识别服务器
static char *send_to_vosk_server(char *file)
{
	static char ret_buf[128];
	
	if((file == NULL)||(file[0] != '/')) {
		return NULL;
	}
	
	int skfd = socket(AF_INET, SOCK_STREAM, 0);
	if(skfd < 0) {
		return NULL;
	}
	
	struct sockaddr_in addr;
	addr.sin_family = AF_INET;
	addr.sin_port = htons(PORT);
	addr.sin_addr.s_addr = inet_addr(IP);
	
	if(connect(skfd, (struct sockaddr*)&addr, sizeof(addr)) < 0) {
		close(skfd);
		return NULL;
	}
	
	send(skfd, file, strlen(file)+1, 0);
	int ret = recv(skfd, ret_buf, sizeof(ret_buf)-1, 0);
	close(skfd);
	
	if(ret > 0) {
		ret_buf[ret] = '\0';
		return ret_buf;
	}
	return NULL;
}

// 语音识别技能
static void recognize_skill_voice(void)
{
	if (!is_my_turn || game_over) return;
	
	// 录音2秒
	audio_record_init(NULL, PCM_SAMPLE_RATE, 1, 16);
	pcm_info_st pcm_info;
	uint8_t *pcm_buf = audio_record(2000, &pcm_info);
	
	if(pcm_info.sampleRate != PCM_SAMPLE_RATE) {
		uint8_t *pcm_buf2 = pcm_s16_mono_resample(pcm_buf, &pcm_info, PCM_SAMPLE_RATE, &pcm_info);
		pcm_free_buf(pcm_buf);
		pcm_buf = pcm_buf2;
	}
	
	pcm_write_wav_file(pcm_buf, &pcm_info, "/tmp/skill.wav");
	pcm_free_buf(pcm_buf);
	
	char *result = send_to_vosk_server("/tmp/skill.wav");
	if (result) {
		printf("Voice recognition: %s\n", result);
		// 检查识别结果
		if (strstr(result, "飞沙走石") || strstr(result, "fei sha zou shi")) {
			if (!skill_used[SKILL_FLY_SAND]) {
				selected_skill = SKILL_FLY_SAND;
			}
		} else if (strstr(result, "拾金不昧") || strstr(result, "shi jin bu mei")) {
			if (!skill_used[SKILL_PICK_GOLD]) {
				selected_skill = SKILL_PICK_GOLD;
				pick_gold_state = 0;
			}
		} else if (strstr(result, "静如止水") || strstr(result, "jing ru zhi shui")) {
			if (!skill_used[SKILL_CALM_WATER]) {
				selected_skill = SKILL_CALM_WATER;
			}
		} else if (strstr(result, "时光倒流") || strstr(result, "shi guang dao liu")) {
			if (!skill_used[SKILL_TIME_REWIND]) {
				selected_skill = SKILL_TIME_REWIND;
			}
		} else if (strstr(result, "力拔山兮") || strstr(result, "li ba shan xi")) {
			if (!skill_used[SKILL_POWER_UP] && energy_count >= 3) {
				selected_skill = SKILL_POWER_UP;
			}
		}
	}
}

// 使用指定种子放置能量果实（确保双方一致）
static void place_energy_fruits_with_seed(unsigned int seed)
{
	// 清除旧的能量果实
	for (int i = 0; i < 5; i++) {
		if (energy_fruits[i][0] >= 0 && energy_fruits[i][1] >= 0) {
			if (board[energy_fruits[i][0]][energy_fruits[i][1]] == 3) {
				board[energy_fruits[i][0]][energy_fruits[i][1]] = 0;
			}
		}
		energy_fruits[i][0] = -1;
		energy_fruits[i][1] = -1;
	}
	
	// 使用指定种子
	unsigned int old_seed = (unsigned int)time(NULL);
	srand(seed);
	
	energy_fruits_placed = 0;
	for (int i = 0; i < 5; i++) {
		int row, col;
		int attempts = 0;
		do {
			row = rand() % BOARD_SIZE;
			col = rand() % BOARD_SIZE;
			attempts++;
		} while (board[row][col] != 0 && attempts < 100);
		
		if (board[row][col] == 0) {
			board[row][col] = 3; // 3表示能量果实（对用户不可见）
			energy_fruits[i][0] = row;
			energy_fruits[i][1] = col;
			energy_fruits_placed++;
		}
	}
	
	// 恢复随机种子
	srand(old_seed);
}

// 随机放置能量果实（首次连接时使用）
static void place_energy_fruits(void)
{
	unsigned int seed = (unsigned int)time(NULL);
	place_energy_fruits_with_seed(seed);
	
	// 发送种子给对手
	if (bluetooth_fd >= 0) {
		char seed_msg[32];
		sprintf(seed_msg, "SEED %u\n", seed);
		myWrite_nonblock(bluetooth_fd, seed_msg, strlen(seed_msg));
	}
}

// 绘制加粗线条
static void draw_thick_line(int x1, int y1, int x2, int y2, int color, int thickness)
{
	for (int i = -thickness/2; i <= thickness/2; i++) {
		if (x1 == x2) {
			fb_draw_line(x1 + i, y1, x2 + i, y2, color);
		} else {
			fb_draw_line(x1, y1 + i, x2, y2 + i, color);
		}
	}
}

// 绘制棋盘
static void draw_board(void)
{
	// 清空背景
	fb_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BACKGROUND);
	
	// 根据游戏状态绘制不同界面
	if (game_state == GAME_STATE_WAITING_START) {
		// 开始界面 - 绘制背景图片，缩放以适应屏幕大小
		fb_image *bg_img = fb_read_jpeg_image("./start.jpg");
		if (bg_img) {
			// 缩放图片以适应屏幕
			fb_image *scaled_img = scale_image_to_screen(bg_img);
			if (scaled_img) {
				fb_draw_image(0, 0, scaled_img, 0);
				fb_free_image(scaled_img);
			} else {
				// 缩放失败，直接绘制原图
				fb_draw_image(0, 0, bg_img, 0);
			}
			fb_free_image(bg_img);
		} else {
			// 如果图片加载失败，使用背景色
			fb_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BACKGROUND);
		}
		
		// 标题（放置在屏幕左半边）
		fb_draw_text(50, 150, "五子棋游戏", 36, COLOR_TEXT);
		
		// 角色信息（放置在屏幕左半边）
		char role_text[50];
		if (role_initialized) {
			sprintf(role_text, "你的角色: %s", my_role == 1 ? "黑子" : "白子");
		} else {
			sprintf(role_text, "等待连接...");
		}
		fb_draw_text(50, 220, role_text, 24, COLOR_TEXT);
		
		// 准备状态（放置在屏幕左半边）
		char ready_text[100];
		if (my_ready && opp_ready) {
			sprintf(ready_text, "双方已准备，游戏即将开始");
		} else if (my_ready) {
			sprintf(ready_text, "你已准备，等待对方...");
		} else {
			sprintf(ready_text, "点击下方按钮开始游戏");
		}
		fb_draw_text(50, 280, ready_text, 24, COLOR_TEXT);
		
		// 开始游戏按钮（放置在屏幕左半边）
		int start_btn_x = 50;
		int start_btn_y = 350;
		int start_color = my_ready ? COLOR_BUTTON_SELECTED : COLOR_BUTTON_ACTIVE;
		fb_draw_rect(start_btn_x, start_btn_y, BUTTON_WIDTH, BUTTON_HEIGHT, start_color);
		fb_draw_border(start_btn_x, start_btn_y, BUTTON_WIDTH, BUTTON_HEIGHT, COLOR_BOARD_LINE);
		fb_draw_text(start_btn_x + 20, start_btn_y + BUTTON_HEIGHT / 2 + 10, 
			my_ready ? "已准备" : "开始游戏", 24, COLOR_TEXT);
		
		fb_update();
		return;
	}
	
	if (game_state == GAME_STATE_ENDED) {
		// 结束界面 - 根据获胜方绘制不同的背景图片，缩放以适应屏幕大小
		fb_image *bg_img = NULL;
		if (winner == 1) {
			// 黑子获胜
			bg_img = fb_read_jpeg_image("./bwin.jpg");
		} else if (winner == 2) {
			// 白子获胜
			bg_img = fb_read_jpeg_image("./wwin.jpg");
		}
		if (bg_img) {
			// 缩放图片以适应屏幕
			fb_image *scaled_img = scale_image_to_screen(bg_img);
			if (scaled_img) {
				fb_draw_image(0, 0, scaled_img, 0);
				fb_free_image(scaled_img);
			} else {
				// 缩放失败，直接绘制原图
				fb_draw_image(0, 0, bg_img, 0);
			}
			fb_free_image(bg_img);
		} else {
			// 如果图片加载失败，使用背景色
			fb_draw_rect(0, 0, SCREEN_WIDTH, SCREEN_HEIGHT, COLOR_BACKGROUND);
		}
		
		// 游戏结束标题（放置在屏幕左半边）
		fb_draw_text(50, 150, "游戏结束", 36, COLOR_TEXT);
		
		// 获胜信息（放置在屏幕左半边）
		char win_text[100];
		if (winner == my_role) {
			sprintf(win_text, "恭喜！你获胜了！");
		} else if (winner != 0) {
			sprintf(win_text, "很遗憾，对方获胜");
		} else {
			sprintf(win_text, "游戏结束");
		}
		fb_draw_text(50, 220, win_text, 28, COLOR_TEXT);
		
		// 重新开始状态（放置在屏幕左半边）
		char restart_text[100];
		if (my_restart && opp_restart) {
			sprintf(restart_text, "双方已准备，游戏即将重新开始");
		} else if (my_restart) {
			sprintf(restart_text, "你已准备，等待对方...");
		} else {
			sprintf(restart_text, "点击下方按钮重新开始");
		}
		fb_draw_text(50, 280, restart_text, 24, COLOR_TEXT);
		
		// 重新开始按钮（放置在屏幕左半边）
		int restart_btn_x = 50;
		int restart_btn_y = 350;
		int restart_color = my_restart ? COLOR_BUTTON_SELECTED : COLOR_BUTTON_ACTIVE;
		fb_draw_rect(restart_btn_x, restart_btn_y, BUTTON_WIDTH, BUTTON_HEIGHT, restart_color);
		fb_draw_border(restart_btn_x, restart_btn_y, BUTTON_WIDTH, BUTTON_HEIGHT, COLOR_BOARD_LINE);
		fb_draw_text(restart_btn_x + 20, restart_btn_y + BUTTON_HEIGHT / 2 + 10, 
			my_restart ? "已准备" : "重新开始", 24, COLOR_TEXT);
		
		fb_update();
		return;
	}
	
	// 游戏进行中，绘制棋盘
	int i;
	int start_x = BOARD_MARGIN_X;
	int start_y = BOARD_MARGIN_Y;
	int end_x = start_x + (BOARD_SIZE - 1) * CELL_SIZE;
	int end_y = start_y + (BOARD_SIZE - 1) * CELL_SIZE;
	
	// 绘制加粗的棋盘网格线
	for (i = 0; i < BOARD_SIZE; i++) {
		// 横线
		draw_thick_line(start_x, start_y + i * CELL_SIZE, 
					end_x, start_y + i * CELL_SIZE, COLOR_BOARD_LINE, LINE_THICKNESS);
		// 竖线
		draw_thick_line(start_x + i * CELL_SIZE, start_y,
					start_x + i * CELL_SIZE, end_y, COLOR_BOARD_LINE, LINE_THICKNESS);
	}
	
	// 绘制天元和星位
	int star_positions[] = {3, 7, 11};
	for (i = 0; i < 3; i++) {
		int x = start_x + star_positions[i] * CELL_SIZE;
		for (int j = 0; j < 3; j++) {
			int y = start_y + star_positions[j] * CELL_SIZE;
			fb_draw_circle_filled(x, y, 4, COLOR_BOARD_LINE);
		}
	}
	
	// 绘制已下的棋子和能量果实
	for (i = 0; i < BOARD_SIZE; i++) {
		for (int j = 0; j < BOARD_SIZE; j++) {
			int x = start_x + j * CELL_SIZE;
			int y = start_y + i * CELL_SIZE;
			
			if (board[i][j] == 1) {
				// 黑子
				fb_draw_circle_filled(x, y, STONE_RADIUS, COLOR_BLACK_STONE);
			} else if (board[i][j] == 2) {
				// 白子
				fb_draw_circle_filled(x, y, STONE_RADIUS, COLOR_BLACK_STONE);
				fb_draw_circle_filled(x, y, STONE_RADIUS - 2, COLOR_WHITE_STONE);
			}
			// 能量果实不显示（对用户不可见）
			// else if (board[i][j] == 3) {
			// 	// 能量果实（小黄点）- 已隐藏
			// }
			
			// 高亮选中位置
			if (selected_row == i && selected_col == j) {
				fb_draw_circle(x, y, STONE_RADIUS + 3, COLOR_HIGHLIGHT);
			}
			
			// 拾金不昧预选棋子高亮
			if (selected_skill == SKILL_PICK_GOLD) {
				if (pick_gold_state >= 1 && pick_gold_my_row == i && pick_gold_my_col == j) {
					fb_draw_circle(x, y, STONE_RADIUS + 3, COLOR_BUTTON_SELECTED);
				}
				if (pick_gold_state >= 2 && pick_gold_opp_row == i && pick_gold_opp_col == j) {
					fb_draw_circle(x, y, STONE_RADIUS + 3, COLOR_BUTTON_SELECTED);
				}
			}
		}
	}
	
	// 绘制右侧UI
	int btn_y = UI_RIGHT_Y;
	
	// 技能按钮
	const char *skill_names[] = {"", "飞沙走石", "拾金不昧", "静如止水", "时光倒流", "力拔山兮"};
	
	for (int skill = 1; skill <= 5; skill++) {
		int color;
		if (selected_skill == skill) {
			color = COLOR_BUTTON_SELECTED;
		} else if (skill == SKILL_POWER_UP) {
			// 力拔山兮需要能量条满
			color = (energy_count >= 3 && !skill_used[skill]) ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON_INACTIVE;
		} else {
			color = (!skill_used[skill]) ? COLOR_BUTTON_ACTIVE : COLOR_BUTTON_INACTIVE;
		}
		
		// 绘制按钮
		fb_draw_rect(UI_RIGHT_X, btn_y, BUTTON_WIDTH, BUTTON_HEIGHT, color);
		fb_draw_border(UI_RIGHT_X, btn_y, BUTTON_WIDTH, BUTTON_HEIGHT, COLOR_BOARD_LINE);
		
		// 绘制技能名称（垂直居中：按钮中心位置）
		int text_y = btn_y + BUTTON_HEIGHT / 2 + 10; // 按钮中心 + 字体大小的一半
		fb_draw_text(UI_RIGHT_X + 5, text_y, (char *)skill_names[skill], 20, COLOR_TEXT);
		
		// 力拔山兮显示能量条
		if (skill == SKILL_POWER_UP) {
			int bar_width = BUTTON_WIDTH - 10;
			int bar_x = UI_RIGHT_X + 5;
			int bar_y = btn_y + BUTTON_HEIGHT - 15;
			
			// 背景
			fb_draw_rect(bar_x, bar_y, bar_width, ENERGY_BAR_HEIGHT, COLOR_BUTTON_INACTIVE);
			// 能量条
			int energy_width = (bar_width * energy_count) / 3;
			if (energy_width > 0) {
				fb_draw_rect(bar_x, bar_y, energy_width, ENERGY_BAR_HEIGHT, COLOR_ENERGY_BAR);
			}
		}
		
		btn_y += BUTTON_HEIGHT + BUTTON_SPACING;
	}
	
	// 确定按钮（语音识别按钮已删除）
	int confirm_y = btn_y + 20;
	int confirm_color = (selected_skill != SKILL_NONE || selected_row >= 0) ? 
		COLOR_BUTTON_ACTIVE : COLOR_BUTTON_INACTIVE;
	fb_draw_rect(UI_RIGHT_X, confirm_y, BUTTON_WIDTH, BUTTON_HEIGHT, confirm_color);
	fb_draw_border(UI_RIGHT_X, confirm_y, BUTTON_WIDTH, BUTTON_HEIGHT, COLOR_BOARD_LINE);
	int confirm_text_y = confirm_y + BUTTON_HEIGHT / 2 + 12;
	fb_draw_text(UI_RIGHT_X + 35, confirm_text_y, "确定", 24, COLOR_TEXT);
	
	// 倒计时显示
	char time_text[20];
	sprintf(time_text, "时间: %d秒", time_left);
	int time_y = confirm_y + BUTTON_HEIGHT + 20;
	if (time_y + 30 <= SCREEN_HEIGHT) {
		fb_draw_rect(UI_RIGHT_X, time_y, BUTTON_WIDTH, 30, COLOR_BACKGROUND);
		fb_draw_text(UI_RIGHT_X, time_y + 20, time_text, 20, COLOR_TEXT);
	}
	
	// 状态信息（显示在屏幕底部）
	int status_y = SCREEN_HEIGHT - 40;
	if (status_y < 0) status_y = 0;
	
	char status_text[100];
	if (is_my_turn) {
		sprintf(status_text, "你的回合 (%s)", my_role == 1 ? "黑子" : "白子");
	} else {
		sprintf(status_text, "等待对方下棋...");
	}
	
	// 确保状态栏区域被填充
	fb_draw_rect(0, status_y, SCREEN_WIDTH, 40, COLOR_BACKGROUND);
	fb_draw_text(10, status_y + 25, status_text, 24, COLOR_TEXT);
	
	// 确保整个屏幕都被填充（填充右侧空白区域）
	if (UI_RIGHT_X + BUTTON_WIDTH < SCREEN_WIDTH) {
		fb_draw_rect(UI_RIGHT_X + BUTTON_WIDTH, 0, 
			SCREEN_WIDTH - (UI_RIGHT_X + BUTTON_WIDTH), SCREEN_HEIGHT, COLOR_BACKGROUND);
	}
	
	// 在右下角空白处绘制游戏图片（不遮挡按钮）
	// 按钮区域：x从700到820，y从30到约500
	// 右下角空白区域：x从820到1024，y从500到600
	int img_x = UI_RIGHT_X + BUTTON_WIDTH + 10; // 820 + 10 = 830
	int img_y = 500; // 从倒计时下方开始
	int img_max_w = SCREEN_WIDTH - img_x; // 1024 - 830 = 194
	int img_max_h = SCREEN_HEIGHT - img_y; // 600 - 500 = 100
	
	// 加载并绘制游戏图片，缩放以适应右下角区域
	fb_image *play_img = fb_read_png_image("./play.png");
	if (play_img) {
		// 计算缩放比例，保持宽高比
		int src_w = play_img->pixel_w;
		int src_h = play_img->pixel_h;
		int dst_w = img_max_w;
		int dst_h = img_max_h;
		
		// 计算缩放比例（取较小的比例以保持宽高比）
		float scale_w = (float)dst_w / src_w;
		float scale_h = (float)dst_h / src_h;
		float scale = (scale_w < scale_h) ? scale_w : scale_h;
		
		// 计算缩放后的尺寸
		int scaled_w = (int)(src_w * scale);
		int scaled_h = (int)(src_h * scale);
		
		// 创建缩放后的图片
		fb_image *scaled_play_img = fb_new_image(play_img->color_type, scaled_w, scaled_h, 0);
		if (scaled_play_img) {
			// 使用最近邻插值进行缩放
			if (play_img->color_type == FB_COLOR_RGBA_8888) {
				for (int y = 0; y < scaled_h; y++) {
					int src_y = (int)(y / scale);
					if (src_y >= src_h) src_y = src_h - 1;
					
					for (int x = 0; x < scaled_w; x++) {
						int src_x = (int)(x / scale);
						if (src_x >= src_w) src_x = src_w - 1;
						
						char *src_pixel = play_img->content + src_y * play_img->line_byte + src_x * 4;
						char *dst_pixel = scaled_play_img->content + y * scaled_play_img->line_byte + x * 4;
						
						memcpy(dst_pixel, src_pixel, 4);
					}
				}
			} else if (play_img->color_type == FB_COLOR_RGB_8880) {
				// RGB格式
				for (int y = 0; y < scaled_h; y++) {
					int src_y = (int)(y / scale);
					if (src_y >= src_h) src_y = src_h - 1;
					
					for (int x = 0; x < scaled_w; x++) {
						int src_x = (int)(x / scale);
						if (src_x >= src_w) src_x = src_w - 1;
						
						char *src_pixel = play_img->content + src_y * play_img->line_byte + src_x * 4;
						char *dst_pixel = scaled_play_img->content + y * scaled_play_img->line_byte + x * 4;
						
						memcpy(dst_pixel, src_pixel, 4);
					}
				}
			}
			
			// 绘制缩放后的图片
			fb_draw_image(img_x, img_y, scaled_play_img, 0);
			fb_free_image(scaled_play_img);
		} else {
			// 如果缩放失败，直接绘制原图（会被裁剪）
			fb_draw_image(img_x, img_y, play_img, 0);
		}
		fb_free_image(play_img);
	}
	
	fb_update();
}

// 检查是否五子连珠
static int check_win(int row, int col, int player)
{
	int directions[4][2] = {{0, 1}, {1, 0}, {1, 1}, {1, -1}};
	int i, j;
	
	for (i = 0; i < 4; i++) {
		int count = 1;
		int dx = directions[i][0];
		int dy = directions[i][1];
		
		for (j = 1; j < 5; j++) {
			int new_row = row + dy * j;
			int new_col = col + dx * j;
			if (new_row >= 0 && new_row < BOARD_SIZE &&
				new_col >= 0 && new_col < BOARD_SIZE &&
				board[new_row][new_col] == player) {
				count++;
			} else {
				break;
			}
		}
		
		for (j = 1; j < 5; j++) {
			int new_row = row - dy * j;
			int new_col = col - dx * j;
			if (new_row >= 0 && new_row < BOARD_SIZE &&
				new_col >= 0 && new_col < BOARD_SIZE &&
				board[new_row][new_col] == player) {
				count++;
			} else {
				break;
			}
		}
		
		if (count >= 5) {
			return 1;
		}
	}
	
	return 0;
}

// 下棋
static int place_stone(int row, int col, int player)
{
	if (row < 0 || row >= BOARD_SIZE || col < 0 || col >= BOARD_SIZE) {
		return 0;
	}
	
	if (board[row][col] != 0 && board[row][col] != 3) {
		return 0;
	}
	
	// 检查是否是能量果实
	int was_energy = 0;
	if (board[row][col] == 3) {
		// 只有己方获得能量果实时才增加能量条
		if (player == my_role) {
			energy_count++;
		}
		was_energy = 1;
		board[row][col] = 0;
		// 移除该能量果实
		for (int i = 0; i < 5; i++) {
			if (energy_fruits[i][0] == row && energy_fruits[i][1] == col) {
				energy_fruits[i][0] = -1;
				energy_fruits[i][1] = -1;
				break;
			}
		}
	}
	
	board[row][col] = player;
	
	// 记录历史
	if (history_count < MAX_HISTORY) {
		history[history_count].row = row;
		history[history_count].col = col;
		history[history_count].player = player;
		history[history_count].was_energy_fruit = was_energy;
		history_count++;
	}
	
	// 检查是否获胜
	if (check_win(row, col, player)) {
		game_over = 1;
		winner = player;
		game_state = GAME_STATE_ENDED;
		my_restart = 0;
		opp_restart = 0;
	}
	
	return 1;
}

// 检查全局胜负（检查所有位置）
static void check_global_win(void)
{
	if (game_over) return;
	
	for (int i = 0; i < BOARD_SIZE; i++) {
		for (int j = 0; j < BOARD_SIZE; j++) {
			if (board[i][j] == 1 || board[i][j] == 2) {
				if (check_win(i, j, board[i][j])) {
					game_over = 1;
					winner = board[i][j];
					game_state = GAME_STATE_ENDED;
					my_restart = 0;
					opp_restart = 0;
					return;
				}
			}
		}
	}
}

// 执行技能
static void execute_skill(int skill)
{
	if (skill_used[skill]) return;
	
	char skill_msg[64];
	
	switch (skill) {
	case SKILL_FLY_SAND: // 飞沙走石：丢掉对方一颗棋子
		if (selected_row >= 0 && selected_col >= 0) {
			if (board[selected_row][selected_col] != 0 && 
				board[selected_row][selected_col] != 3 &&
				board[selected_row][selected_col] != my_role) {
				int target_row = selected_row;
				int target_col = selected_col;
				board[target_row][target_col] = 0;
				skill_used[SKILL_FLY_SAND] = 1;
				selected_row = -1;
				selected_col = -1;
				selected_skill = SKILL_NONE;
				// 发送技能使用消息
				if (bluetooth_fd >= 0) {
					sprintf(skill_msg, "SKILL %d %d %d\n", SKILL_FLY_SAND, target_row, target_col);
					myWrite_nonblock(bluetooth_fd, skill_msg, strlen(skill_msg));
				}
				// 技能使用后检查胜负
				check_global_win();
			}
		}
		break;
		
	case SKILL_PICK_GOLD: // 拾金不昧：互换棋子
		if (pick_gold_state == 2) {
			int temp = board[pick_gold_my_row][pick_gold_my_col];
			board[pick_gold_my_row][pick_gold_my_col] = board[pick_gold_opp_row][pick_gold_opp_col];
			board[pick_gold_opp_row][pick_gold_opp_col] = temp;
			skill_used[SKILL_PICK_GOLD] = 1;
			selected_skill = SKILL_NONE;
			pick_gold_state = 0;
			// 发送技能使用消息
			if (bluetooth_fd >= 0) {
				sprintf(skill_msg, "SKILL %d %d %d %d %d\n", SKILL_PICK_GOLD, 
					pick_gold_my_row, pick_gold_my_col, pick_gold_opp_row, pick_gold_opp_col);
				myWrite_nonblock(bluetooth_fd, skill_msg, strlen(skill_msg));
			}
			// 技能使用后检查胜负
			check_global_win();
		}
		break;
		
	case SKILL_CALM_WATER: // 静如止水：时间+60秒
		time_left += 60;
		skill_used[SKILL_CALM_WATER] = 1;
		selected_skill = SKILL_NONE;
		// 发送技能使用消息
		if (bluetooth_fd >= 0) {
			sprintf(skill_msg, "SKILL %d\n", SKILL_CALM_WATER);
			myWrite_nonblock(bluetooth_fd, skill_msg, strlen(skill_msg));
		}
		break;
		
	case SKILL_TIME_REWIND: // 时光倒流：回退一回合
		if (history_count >= 2) {
			// 移除对方上一轮
			history_count--;
			int opp_row = history[history_count].row;
			int opp_col = history[history_count].col;
			int opp_was_energy = history[history_count].was_energy_fruit;
			board[opp_row][opp_col] = 0;
			
			// 如果对方棋子落在能量果实位置，恢复能量果实（但不影响我的能量条）
			if (opp_was_energy) {
				board[opp_row][opp_col] = 3;
				// 恢复能量果实记录
				for (int i = 0; i < 5; i++) {
					if (energy_fruits[i][0] == -1) {
						energy_fruits[i][0] = opp_row;
						energy_fruits[i][1] = opp_col;
						break;
					}
				}
				// 对方获得的能量果实不影响我的能量条
			}
			
			// 移除己方上一轮
			if (history_count > 0) {
				history_count--;
				int my_row = history[history_count].row;
				int my_col = history[history_count].col;
				int my_was_energy = history[history_count].was_energy_fruit;
				int my_player = history[history_count].player;
				board[my_row][my_col] = 0;
				
				// 如果己方棋子落在能量果实位置，恢复能量果实和能量
				if (my_was_energy && my_player == my_role) {
					board[my_row][my_col] = 3;
					// 恢复能量果实记录
					for (int i = 0; i < 5; i++) {
						if (energy_fruits[i][0] == -1) {
							energy_fruits[i][0] = my_row;
							energy_fruits[i][1] = my_col;
							break;
						}
					}
					// 只有己方获得的能量果实才减少能量条
					energy_count--;
				} else if (my_was_energy) {
					// 对方获得的能量果实，只恢复果实位置，不减少能量条
					board[my_row][my_col] = 3;
					for (int i = 0; i < 5; i++) {
						if (energy_fruits[i][0] == -1) {
							energy_fruits[i][0] = my_row;
							energy_fruits[i][1] = my_col;
							break;
						}
					}
				}
			}
			
			skill_used[SKILL_TIME_REWIND] = 1;
			selected_skill = SKILL_NONE;
			// 发送技能使用消息
			if (bluetooth_fd >= 0) {
				sprintf(skill_msg, "SKILL %d\n", SKILL_TIME_REWIND);
				myWrite_nonblock(bluetooth_fd, skill_msg, strlen(skill_msg));
			}
			// 技能使用后检查胜负
			check_global_win();
		}
		break;
		
	case SKILL_POWER_UP: // 力拔山兮：清空棋盘
		if (energy_count >= 3) {
			// 清空棋盘（保留能量果实）
			for (int i = 0; i < BOARD_SIZE; i++) {
				for (int j = 0; j < BOARD_SIZE; j++) {
					if (board[i][j] == 1 || board[i][j] == 2) {
						board[i][j] = 0;
					}
				}
			}
			energy_count = 0;
			skill_used[SKILL_POWER_UP] = 1;
			selected_skill = SKILL_NONE;
			history_count = 0; // 清空历史
			// 发送技能使用消息
			if (bluetooth_fd >= 0) {
				sprintf(skill_msg, "SKILL %d\n", SKILL_POWER_UP);
				myWrite_nonblock(bluetooth_fd, skill_msg, strlen(skill_msg));
			}
			// 清空棋盘后检查胜负（应该没有胜负）
			check_global_win();
		}
		break;
	}
}

// 倒计时回调
static void timer_cb(int period)
{
	if (game_state != GAME_STATE_PLAYING || !is_my_turn || game_over) return;
	
	time_left--;
	if (time_left <= 0) {
		time_left = 0;
		// 时间到，随机选择一个位置落子
		if (current_player == my_role) {
			int attempts = 0;
			int row, col;
			do {
				row = rand() % BOARD_SIZE;
				col = rand() % BOARD_SIZE;
				attempts++;
			} while ((board[row][col] != 0 && board[row][col] != 3) && attempts < 100);
			
			if (board[row][col] == 0 || board[row][col] == 3) {
				if (place_stone(row, col, current_player)) {
					if (bluetooth_fd >= 0) {
						char move_msg[32];
						sprintf(move_msg, "MOVE %d %d\n", row, col);
						myWrite_nonblock(bluetooth_fd, move_msg, strlen(move_msg));
					}
					current_player = (current_player == 1) ? 2 : 1;
					is_my_turn = 0;
					time_left = 30;
					selected_row = -1;
					selected_col = -1;
				}
			}
		}
	}
	draw_board();
}

// 蓝牙初始化
static int bluetooth_tty_init(const char *dev)
{
	int fd = open(dev, O_RDWR|O_NOCTTY|O_NONBLOCK);
	if(fd < 0){
		printf("bluetooth_tty_init open %s error(%d): %s\n", dev, errno, strerror(errno));
		return -1;
	}
	return fd;
}

// 蓝牙接收回调
static void bluetooth_tty_event_cb(int fd)
{
	char buf[256];
	int n;
	
	n = myRead_nonblock(fd, buf, sizeof(buf)-1);
	if(n <= 0) {
		if (n < 0 && errno != EAGAIN) {
			printf("bluetooth read error\n");
		}
		return;
	}
	
	buf[n] = '\0';
	printf("bluetooth receive: %s\n", buf);
	
	// 解析消息
	if (strncmp(buf, "INIT ", 5) == 0) {
		int opponent_role;
		if (sscanf(buf + 5, "%d", &opponent_role) == 1) {
			if (!role_initialized) {
				my_role = (opponent_role == 1) ? 2 : 1;
				role_initialized = 1;
				current_player = 1;
				is_my_turn = (my_role == 1) ? 1 : 0;
				time_left = 30;
				game_state = GAME_STATE_WAITING_START;
				// 等待接收SEED消息来放置能量果实
				draw_board();
			}
		}
	} else if (strncmp(buf, "READY", 5) == 0) {
		// 对方准备好开始
		opp_ready = 1;
		if (my_ready && opp_ready && game_state == GAME_STATE_WAITING_START) {
			// 双方都准备好，开始游戏
			game_state = GAME_STATE_PLAYING;
			game_over = 0;
		}
		draw_board();
	} else if (strncmp(buf, "RESTART", 7) == 0) {
		// 对方准备好重新开始
		opp_restart = 1;
		if (my_restart && opp_restart && game_state == GAME_STATE_ENDED) {
			// 双方都准备好，重新开始游戏
			game_state = GAME_STATE_WAITING_START;
			game_over = 0;
			winner = 0;
			memset(board, 0, sizeof(board));
			current_player = 1;
			is_my_turn = (my_role == 1) ? 1 : 0;
			time_left = 30;
			memset(skill_used, 0, sizeof(skill_used));
			energy_count = 0;
			history_count = 0;
			selected_skill = SKILL_NONE;
			selected_row = -1;
			selected_col = -1;
			my_ready = 0;
			opp_ready = 0;
			my_restart = 0;
			opp_restart = 0;
			place_energy_fruits();
		}
		draw_board();
	} else if (strncmp(buf, "MOVE ", 5) == 0) {
		int row, col;
		if (sscanf(buf + 5, "%d %d", &row, &col) == 2) {
			if (game_state == GAME_STATE_PLAYING && !game_over && role_initialized) {
				if (place_stone(row, col, current_player)) {
					current_player = (current_player == 1) ? 2 : 1;
					is_my_turn = 1;
					time_left = 30;
					draw_board();
				}
			}
		}
	} else if (strncmp(buf, "SEED ", 5) == 0) {
		// 接收能量果实种子
		unsigned int seed;
		if (sscanf(buf + 5, "%u", &seed) == 1) {
			place_energy_fruits_with_seed(seed);
			draw_board();
		}
	} else if (strncmp(buf, "SKILL ", 6) == 0) {
		// 接收技能使用消息
		int skill, row1, col1, row2, col2;
		if (sscanf(buf + 6, "%d %d %d %d %d", &skill, &row1, &col1, &row2, &col2) >= 1) {
			switch (skill) {
			case SKILL_FLY_SAND:
				if (sscanf(buf + 6, "%d %d %d", &skill, &row1, &col1) == 3) {
					if (row1 >= 0 && row1 < BOARD_SIZE && col1 >= 0 && col1 < BOARD_SIZE) {
						board[row1][col1] = 0;
					}
				}
				check_global_win();
				break;
			case SKILL_PICK_GOLD:
				if (sscanf(buf + 6, "%d %d %d %d %d", &skill, &row1, &col1, &row2, &col2) == 5) {
					if (row1 >= 0 && row1 < BOARD_SIZE && col1 >= 0 && col1 < BOARD_SIZE &&
						row2 >= 0 && row2 < BOARD_SIZE && col2 >= 0 && col2 < BOARD_SIZE) {
						int temp = board[row1][col1];
						board[row1][col1] = board[row2][col2];
						board[row2][col2] = temp;
					}
				}
				check_global_win();
				break;
			case SKILL_TIME_REWIND:
				// 对方使用时光倒流，回退两回合
				if (history_count >= 2) {
					history_count--;
					int opp_row = history[history_count].row;
					int opp_col = history[history_count].col;
					int opp_was_energy = history[history_count].was_energy_fruit;
					int opp_player = history[history_count].player;
					board[opp_row][opp_col] = 0;
					
					// 如果对方棋子落在能量果实位置，恢复能量果实
					if (opp_was_energy) {
						board[opp_row][opp_col] = 3;
						for (int i = 0; i < 5; i++) {
							if (energy_fruits[i][0] == -1) {
								energy_fruits[i][0] = opp_row;
								energy_fruits[i][1] = opp_col;
								break;
							}
						}
						// 只有是我获得的能量果实才减少我的能量条
						if (opp_player == my_role) {
							energy_count--;
						}
					}
					
					if (history_count > 0) {
						history_count--;
						int my_row = history[history_count].row;
						int my_col = history[history_count].col;
						int my_was_energy = history[history_count].was_energy_fruit;
						int my_player = history[history_count].player;
						board[my_row][my_col] = 0;
						
						// 如果己方棋子落在能量果实位置，恢复能量果实和能量
						if (my_was_energy) {
							board[my_row][my_col] = 3;
							for (int i = 0; i < 5; i++) {
								if (energy_fruits[i][0] == -1) {
									energy_fruits[i][0] = my_row;
									energy_fruits[i][1] = my_col;
									break;
								}
							}
							// 只有是我获得的能量果实才减少我的能量条
							if (my_player == my_role) {
								energy_count--;
							}
						}
					}
				}
				check_global_win();
				break;
			case SKILL_POWER_UP:
				// 对方使用力拔山兮，清空棋盘
				for (int i = 0; i < BOARD_SIZE; i++) {
					for (int j = 0; j < BOARD_SIZE; j++) {
						if (board[i][j] == 1 || board[i][j] == 2) {
							board[i][j] = 0;
						}
					}
				}
				history_count = 0;
				check_global_win();
				break;
			}
			draw_board();
		}
	} else if (strncmp(buf, "RESET", 5) == 0) {
		game_state = GAME_STATE_WAITING_START;
		memset(board, 0, sizeof(board));
		current_player = 1;
		game_over = 0;
		winner = 0;
		is_my_turn = (my_role == 1) ? 1 : 0;
		time_left = 30;
		memset(skill_used, 0, sizeof(skill_used));
		energy_count = 0;
		history_count = 0;
		selected_skill = SKILL_NONE;
		selected_row = -1;
		selected_col = -1;
		my_ready = 0;
		opp_ready = 0;
		my_restart = 0;
		opp_restart = 0;
		place_energy_fruits();
		draw_board();
	}
}

// 触屏事件回调
static void touch_event_cb(int fd)
{
	int type, x, y, finger;
	type = touch_read(fd, &x, &y, &finger);
	
	switch(type){
	case TOUCH_PRESS:
		// 处理开始界面
		if (game_state == GAME_STATE_WAITING_START) {
			if (role_initialized) {
				// 检查是否点击开始游戏按钮（放置在屏幕左半边）
				int start_btn_x = 50;
				int start_btn_y = 350;
				if (check_button_click(x, y, start_btn_x, start_btn_y, BUTTON_WIDTH, BUTTON_HEIGHT)) {
					if (!my_ready) {
						my_ready = 1;
						// 发送准备消息
						if (bluetooth_fd >= 0) {
							myWrite_nonblock(bluetooth_fd, "READY\n", 6);
						}
						// 检查是否可以开始
						if (my_ready && opp_ready) {
							game_state = GAME_STATE_PLAYING;
							game_over = 0;
						}
						draw_board();
					}
				}
			} else {
				// 未初始化，点击任意位置初始化
				if (bluetooth_fd >= 0) {
					my_role = 1;
					char init_msg[32];
					sprintf(init_msg, "INIT %d\n", my_role);
					myWrite_nonblock(bluetooth_fd, init_msg, strlen(init_msg));
					role_initialized = 1;
					is_my_turn = 1;
					time_left = 30;
					game_state = GAME_STATE_WAITING_START;
					// 作为先手，生成能量果实并发送种子
					place_energy_fruits();
					draw_board();
				}
			}
			break;
		}
		
		// 处理结束界面
		if (game_state == GAME_STATE_ENDED) {
			// 检查是否点击重新开始按钮（放置在屏幕左半边）
			int restart_btn_x = 50;
			int restart_btn_y = 350;
			if (check_button_click(x, y, restart_btn_x, restart_btn_y, BUTTON_WIDTH, BUTTON_HEIGHT)) {
				if (!my_restart) {
					my_restart = 1;
					// 发送重新开始消息
					if (bluetooth_fd >= 0) {
						myWrite_nonblock(bluetooth_fd, "RESTART\n", 8);
					}
					// 检查是否可以重新开始
					if (my_restart && opp_restart) {
						game_state = GAME_STATE_WAITING_START;
						game_over = 0;
						winner = 0;
						memset(board, 0, sizeof(board));
						current_player = 1;
						is_my_turn = (my_role == 1) ? 1 : 0;
						time_left = 30;
						memset(skill_used, 0, sizeof(skill_used));
						energy_count = 0;
						history_count = 0;
						selected_skill = SKILL_NONE;
						selected_row = -1;
						selected_col = -1;
						my_ready = 0;
						opp_ready = 0;
						my_restart = 0;
						opp_restart = 0;
						place_energy_fruits();
					}
					draw_board();
				}
			}
			break;
		}
		
		// 游戏进行中的处理
		if (game_state != GAME_STATE_PLAYING) break;
		
		if (!is_my_turn) break;
		
		// 检查是否点击确定按钮
		int confirm_y = UI_RIGHT_Y + 5 * (BUTTON_HEIGHT + BUTTON_SPACING) + 20;
		if (check_button_click(x, y, UI_RIGHT_X, confirm_y, BUTTON_WIDTH, BUTTON_HEIGHT)) {
			if (game_state == GAME_STATE_PLAYING) {
				if (selected_skill != SKILL_NONE) {
					execute_skill(selected_skill);
					draw_board();
				} else if (selected_row >= 0 && selected_col >= 0) {
					// 确认下棋
					if (current_player == my_role && place_stone(selected_row, selected_col, current_player)) {
						if (bluetooth_fd >= 0) {
							char move_msg[32];
							sprintf(move_msg, "MOVE %d %d\n", selected_row, selected_col);
							myWrite_nonblock(bluetooth_fd, move_msg, strlen(move_msg));
						}
						current_player = (current_player == 1) ? 2 : 1;
						is_my_turn = 0;
						time_left = 30;
						selected_row = -1;
						selected_col = -1;
						draw_board();
					}
				}
			}
			break;
		}
		
		// 检查是否点击技能按钮
		int btn_y = UI_RIGHT_Y;
		for (int skill = 1; skill <= 5; skill++) {
			if (check_button_click(x, y, UI_RIGHT_X, btn_y, BUTTON_WIDTH, BUTTON_HEIGHT)) {
				if (skill == SKILL_POWER_UP) {
					if (energy_count >= 3 && !skill_used[skill]) {
						// 如果已选中，则取消选中
						if (selected_skill == skill) {
							selected_skill = SKILL_NONE;
						} else {
							selected_skill = skill;
						}
					}
				} else {
					if (!skill_used[skill]) {
						// 如果已选中，则取消选中
						if (selected_skill == skill) {
							selected_skill = SKILL_NONE;
							pick_gold_state = 0;
							pick_gold_my_row = -1;
							pick_gold_my_col = -1;
							pick_gold_opp_row = -1;
							pick_gold_opp_col = -1;
						} else {
							selected_skill = skill;
							if (skill == SKILL_PICK_GOLD) {
								pick_gold_state = 0;
								pick_gold_my_row = -1;
								pick_gold_my_col = -1;
								pick_gold_opp_row = -1;
								pick_gold_opp_col = -1;
							}
						}
					}
				}
				selected_row = -1;
				selected_col = -1;
				draw_board();
				break;
			}
			btn_y += BUTTON_HEIGHT + BUTTON_SPACING;
		}
		
		// 检查是否点击棋盘
		int row, col;
		if (screen_to_board(x, y, &row, &col)) {
			if (selected_skill == SKILL_PICK_GOLD) {
				// 拾金不昧：需要选择两个棋子，可以改变预选选中的棋子
				if (board[row][col] == my_role) {
					// 点击己方棋子：重新选择己方棋子
					pick_gold_my_row = row;
					pick_gold_my_col = col;
					// 如果之前已选对方棋子，清除对方选择
					if (pick_gold_state == 2) {
						pick_gold_opp_row = -1;
						pick_gold_opp_col = -1;
						pick_gold_state = 1;
					} else {
						pick_gold_state = 1;
					}
				} else if (board[row][col] != 0 && board[row][col] != 3 && board[row][col] != my_role) {
					// 点击对方棋子：选择或重新选择对方棋子
					if (pick_gold_state >= 1) {
						// 已选己方棋子，可以选择或重新选择对方棋子
						pick_gold_opp_row = row;
						pick_gold_opp_col = col;
						pick_gold_state = 2;
					}
				}
			} else if (selected_skill == SKILL_FLY_SAND) {
				// 飞沙走石：选择对方棋子
				selected_row = row;
				selected_col = col;
			} else {
				// 普通下棋：选择位置
				if (board[row][col] == 0 || board[row][col] == 3) {
					selected_row = row;
					selected_col = col;
					selected_skill = SKILL_NONE;
				}
			}
			draw_board();
		}
		break;
		
	case TOUCH_ERROR:
		printf("close touch fd\n");
		task_delete_file(fd);
		close(fd);
		break;
	default:
		return;
	}
}

int main(int argc, char *argv[])
{
	fb_init("/dev/fb0");
	font_init("./font.ttc");
	
	memset(board, 0, sizeof(board));
	current_player = 1;
	game_state = GAME_STATE_WAITING_START;
	game_over = 0;
	winner = 0;
	my_role = 1;
	role_initialized = 0;
	is_my_turn = 0;
	time_left = 30;
	memset(skill_used, 0, sizeof(skill_used));
	energy_count = 0;
	history_count = 0;
	selected_skill = SKILL_NONE;
	selected_row = -1;
	selected_col = -1;
	my_ready = 0;
	opp_ready = 0;
	my_restart = 0;
	opp_restart = 0;
	
	srand(time(NULL));
	
	draw_board();
	
	touch_fd = touch_init("/dev/input/event2");
	if (touch_fd >= 0) {
		task_add_file(touch_fd, touch_event_cb);
	}
	
	bluetooth_fd = bluetooth_tty_init("/dev/rfcomm0");
	if (bluetooth_fd >= 0) {
		task_add_file(bluetooth_fd, bluetooth_tty_event_cb);
		printf("Bluetooth connected\n");
	} else {
		printf("Bluetooth connection failed, running in local mode\n");
	}
	
	// 添加定时器
	task_add_timer(timer_period, timer_cb);
	
	// 添加语音识别（可以通过长按或其他方式触发）
	// 这里暂时在每次触摸时检查，实际可以添加专门的语音触发按钮
	
	task_loop();
	
	return 0;
}
