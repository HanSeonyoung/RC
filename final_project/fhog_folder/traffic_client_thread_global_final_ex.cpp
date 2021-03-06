#include <dlib/image_processing.h>
#include <dlib/image_transforms.h>
#include <dlib/gui_widgets.h>
#include <dlib/svm_threaded.h>
#include <dlib/gui_widgets.h>
#include <dlib/data_io.h>
#include <dlib/opencv.h>

#include <opencv2/core/core.hpp>
#include <opencv2/highgui/highgui.hpp>
#include <opencv2/imgproc/imgproc.hpp>
#include <opencv2/opencv.hpp>
#include <opencv2/core/mat.hpp>  
#include <opencv2/imgcodecs.hpp>  

#include <iostream>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <ctime>
#include <cstdio>
#include <cstdlib>

#include <fcntl.h>
#include <sys/socket.h> 
#include <pthread.h>
#include <arpa/inet.h>
#include <unistd.h>
#include <sys/stat.h>
#include <errno.h>
#include <time.h>
#include <semaphore.h>

using namespace std;
using namespace cv;

namespace HSV_CONST
{
	enum
	{
		RED_HL =170, RED_SL =110, RED_VL =150,\
		RED_HH =179, RED_SH =255, RED_VH =255,\
		GREEN_HL =40, GREEN_SL =35, GREEN_VL =35,\
		GREEN_HH =80, GREEN_SH =255, GREEN_VH =255
	};
}

void* img_recv(void*);
void* img_handler(void*);
void* img_show(void*);
void* img_detect_ts(void*);
void* img_detect_tl(void*);
void* snd_handler_ts(void*);
void* snd_play(void*);
void create_msg_box(vector<dlib::rectangle> &, dlib::point* (&), Point* (&), int);
void create_color_box(Mat* (&), Point* (&), int);
void create_msg_line(int, char*, Point* (&));
float dist_detect(int, char*, Point* (&));
int tlight_msg_handler(Mat &);
int tsign_msg_handler(Mat &, int, dlib::rectangle &, char*);
int hsv_handler(Mat &);
int red_detect(Mat &);
int green_detect(Mat &);

// 전역변수 class private로 바꾸기
// 함수 깔끔하게 정리


Mat img;
sem_t recv_sync;
sem_t ts_sync_snd;
sem_t snd_sync_ts;
sem_t ts_print;
sem_t ts_delete;
sem_t tl_print;
sem_t tl_delete;

int red_sign_on, child_sign_on, buf, sw;

typedef dlib::scan_fhog_pyramid<dlib::pyramid_down<6>> image_scanner_type; 

dlib::object_detector<image_scanner_type> detector_tlight;
Mat*Color;
dlib::point*array_dpt_tl;
Point*array_cpt_tl;

dlib::object_detector<image_scanner_type> detector_tsign;
dlib::point*array_dpt_ts;
Point*array_cpt_ts;

vector<dlib::rectangle> dets_tlight;
vector<dlib::rectangle> dets_tsign;

int tlight_size;
int tsign_size;

int main(int argc, char** argv)
{

	int connSock;
	struct sockaddr_in server_addr;
	char*serverAddr;
	int serverPort; 
	int thr_id[2], status;
	pthread_t pid[2];
	int len;
	
	img = Mat::zeros(240, 320, CV_8UC3);
	
	if(argc < 3)
	{
		perror("Usage: IP_address Port_num");
		return -1;
	}

	serverAddr = argv[1];
	serverPort = atoi(argv[2]);

	if((connSock=socket(PF_INET, SOCK_STREAM, 0)) < 0) 
	{
		perror("Traffic client can't open socket");
		return -1;
	}

	memset(&server_addr, 0, sizeof(server_addr));
	server_addr.sin_family = AF_INET;
	server_addr.sin_addr.s_addr = inet_addr(serverAddr);
	server_addr.sin_port = htons(serverPort);

	if(connect(connSock, (struct sockaddr *)&server_addr, sizeof(server_addr)) < 0) 
	{
		perror("Traffic client can't connect");
		return -1;
	}

	printf("Traffic Client connect to Traffic server \n");

	if(thr_id[0]=pthread_create(&pid[0], NULL, img_recv, NULL) < 0)
	{
		perror("pthread1 failed");
		return -1;
	}
	if(thr_id[1]=pthread_create(&pid[1], NULL, img_handler, (void*)&connSock) < 0)
	{
		perror("pthread2 failed");
		return -1;
	}
/*	if(thr_id[2]=pthread_create(&pid[2], NULL, img_show, (void*)&connSock) < 0)
	{
		perror("pthread2 failed");
		return -1;
	}*/

	pthread_join(pid[0], (void**)&status);
	pthread_join(pid[1], (void**)&status);
//	pthread_join(pid[2], (void**)&status);

	close(connSock);

	return 0;
}

void* img_recv(void*arg)
{
	VideoCapture vcap; 
	pthread_t pid;
	int status;
 
	if(pthread_create(&pid, NULL, snd_handler_ts, NULL) < 0)
	{
		perror("pthread_tl failed");
	}

	string videoStreamAddress = "rtsp://192.168.1.105:8554/test";	
	
	vcap.open(videoStreamAddress);
	vcap.set(CV_CAP_PROP_CONVERT_RGB,1);

	while(1) 
	{ 
		vcap.read(img);
		//sem_post(&recv_sync);	
	} 		

	pthread_join(pid, (void**)&status);
}

void* img_handler(void*arg)
{
	int connSock = *(int*)arg;
	pthread_t pid[3];
	int status;
	int detect_color = 0;

	char dist_msg[15];
	int size = 50;

	//sem_wait(&recv_sync);

	if(pthread_create(&pid[0], NULL, img_detect_ts, (void*)&connSock) < 0)
	{
		perror("pthread_ts failed");
	}
	
	if(pthread_create(&pid[1], NULL, img_detect_tl, (void*)&connSock) < 0)
	{
		perror("pthread_tl failed");
	}
	
	while(1)
	{
		if(tlight_size)
		{
			Color = new Mat[tlight_size];
			array_dpt_tl = new dlib::point[tlight_size<<1];
			array_cpt_tl = new Point[tlight_size<<1];

			create_msg_box(dets_tlight, array_dpt_tl, array_cpt_tl, tlight_size);
			create_color_box(Color, array_cpt_tl, tlight_size);

			for(int i=0;i<tlight_size;i++)
			{
				detect_color = tlight_msg_handler(Color[i]);

				float distance=dist_detect(size, dist_msg, array_cpt_tl);

				create_msg_line((tlight_size<<1)+detect_color, dist_msg, array_cpt_tl);

				if(detect_color>>1)
				{
					cout<<"detected traffic light GREEN "<<endl;
					red_sign_on = 0;
				}
				else
				{
					if(detect_color)
					{
						cout<<"detected traffic light RED == STOP!!"<<endl;
	//					if(distance < 50)
							red_sign_on = 2;
					}
					else
						red_sign_on = 0;
				}
			}

			for(int i=0;i<tlight_size;i++)
			{
				rectangle(img, array_cpt_tl[i*2], array_cpt_tl[i*2+1], cv::Scalar(0, 255, 0),3);
			}

			delete []Color;
			delete []array_dpt_tl;
			delete []array_cpt_tl;
		}
		else if(tsign_size)
		{
			array_dpt_ts=new dlib::point[tsign_size<<1];
			array_cpt_ts=new Point[tsign_size<<1];

			if( sw == 0 )
			{
				int pid = fork();
				if(pid == 0)
				{
					int ret = system("mpg123 child_sign_voice.mp3");
					return NULL;
				}
			}

			sw = 1;

			cout<<"deteced child == "<<tsign_size<<' '<<"slow!!"<<endl;
			
			child_sign_on = 1;

			create_msg_box(dets_tsign, array_dpt_ts, array_cpt_ts, tsign_size);
			float distance=dist_detect(size, dist_msg, array_cpt_ts);
			create_msg_line(tsign_size, dist_msg, array_cpt_ts);

			for(int i=0;i<tsign_size;i++)
			{
				rectangle(img, array_cpt_ts[i*2], array_cpt_ts[i*2+1], cv::Scalar(0, 255, 0),3);
			}

			delete []array_dpt_ts;
			delete []array_cpt_ts;
		}
		else
		{
			red_sign_on = 0;
			child_sign_on = 0;
		}

		buf= red_sign_on | child_sign_on; // 0 fast, 1 slow, 2 stop, 3 slow and stop

		if(send(connSock, &buf, sizeof(buf), 0) < 0) 
		{
			perror("send to traffic server failed");
		}
	
		imshow("Output Window", img); 
		waitKey(1);
	}

	pthread_join(pid[0], (void**)&status);
	pthread_join(pid[1], (void**)&status);
}

void* img_detect_ts(void*arg)
{
	int pid;
	
	image_scanner_type scanner_ts;

	dlib::matrix <dlib::bgr_pixel>cimg;

	dlib::deserialize("tsign_detector.svm") >> detector_tsign;

	scanner_ts.set_detection_window_size(80, 80); 
	scanner_ts.set_max_pyramid_levels(1); 

	while(1)
	{	
		dlib::assign_image(cimg,dlib::cv_image<dlib::bgr_pixel>(img));
		
		dlib::pyramid_up(cimg);	

		//vector<dlib::rectangle> dets_tsign;

		dets_tsign = detector_tsign(cimg);

		tsign_size = dets_tsign.size();
	}
}

void* img_detect_tl(void*arg)
{
	image_scanner_type scanner_tl;

	dlib::matrix <dlib::bgr_pixel>cimg;

	dlib::deserialize("tlight3_detector.svm") >> detector_tlight;

	scanner_tl.set_detection_window_size(80, 170); 
	scanner_tl.set_max_pyramid_levels(2);	

	while(1)
	{
		dlib::assign_image(cimg,dlib::cv_image<dlib::bgr_pixel>(img));
	
		dlib::pyramid_up(cimg);	

		//vector<dlib::rectangle> dets_tlight;

		dets_tlight = detector_tlight(cimg);

		tlight_size = dets_tlight.size();

	}
}

void create_msg_box(vector<dlib::rectangle> &dets, dlib::point*(&array_dpt), Point*(&array_cpt), int size)
{
	for(int i=0;i<size;i++)
	{
		array_dpt[i*2]=dets[i].tl_corner();
		array_dpt[i*2+1]=dets[i].br_corner();

		array_cpt[i*2].x=array_dpt[i*2].x()>>1;
		array_cpt[i*2].y=array_dpt[i*2].y()>>1;
		array_cpt[i*2+1].x=array_dpt[i*2+1].x()>>1;
		array_cpt[i*2+1].y=array_dpt[i*2+1].y()>>1;
	}
}

void create_color_box(Mat* (&Color), Point*(&array_cpt_tl), int size)
{
	for(int i=0;i<size;i++)
	{
		if(array_cpt_tl[i*2].x < 0)
		{
			if(abs(array_cpt_tl[i*2].x) < img.cols>>1)
				array_cpt_tl[i*2].x = 0;
		}

		if(array_cpt_tl[i*2+1].x > img.cols)
		{
			array_cpt_tl[i*2+1].x = img.cols;
		}

		if(array_cpt_tl[i*2].y < 0)
		{
			if(abs(array_cpt_tl[i*2].y) < img.rows>>1) 
				array_cpt_tl[i*2].y = 0;
		}

		if(array_cpt_tl[i*2+1].y > img.rows)
		{
			array_cpt_tl[i*2+1].y = img.rows;
		}

	}

	for(int i=0;i<size;i++)
	{
		Rect roi(array_cpt_tl[i*2].x, array_cpt_tl[i*2].y, array_cpt_tl[i*2+1].x - array_cpt_tl[i*2].x, array_cpt_tl[i*2+1].y - array_cpt_tl[i*2].y);
		Color[i]=img(roi);
	}
}	

void create_msg_line(int t_size, char*dist_msg, Point*(&array_cpt))
{
	int font = 1;
	int fscale = 1;
	int thick = 1;
	int ftype = CV_AA;
	char put_msg[15];

		if(!(t_size>>1))
		{
			strcpy(put_msg, "Child");
			putText(img, put_msg, Point(array_cpt[0].x, array_cpt[0].y), font, fscale, Scalar::all(255), thick, ftype);
			putText(img, dist_msg, Point(10, img.rows - 10), font, fscale, Scalar::all(255), thick, ftype);
		}
		else
		{
			if(t_size>>2)
			{
				strcpy(put_msg, "GREEN");
				putText(img, put_msg, Point(array_cpt[0].x, array_cpt[0].y), font, fscale, Scalar::all(255), thick, ftype);
				putText(img, dist_msg, Point(img.cols - 90, img.rows - 10), font, fscale, Scalar::all(255), thick, ftype);
			}
			else
			{
				if(t_size&1)
				{
					strcpy(put_msg, "RED");
					putText(img, put_msg, Point(array_cpt[0].x, array_cpt[0].y), font, fscale, Scalar::all(255), thick, ftype);
					putText(img, dist_msg, Point(img.cols - 90, img.rows - 10), font, fscale, Scalar::all(255), thick, ftype);
				}
			}
		}
}

void* snd_handler_ts(void*arg)
{
	while(1)
	{
		//sem_wait(&ts_sync_snd);

		if(child_sign_on)
		{
		//	sem_post(&snd_sync_ts);
			
			int ret = system("mpg123 child_sign_voice.mp3");
		}
	}
}

int tlight_msg_handler(Mat &img)
{
	int i = 0;
	int red_positive;
		
	red_positive = hsv_handler(img);

	if(red_positive>>1)
	{
		//cout<<endl;
		//cout<<"redsign == Stop"<<' '<<i<<endl;
		//cout<<endl;
		i++;
		return 2;
	}
	else
	{
		//cout<<endl;
		//cout<<"no red sign == Go"<<endl;
		if(red_positive)
			return 1;
	}

	return 0;
}

float dist_detect(int size, char*dist_msg, Point*(&array_cpt))
{
	float v;
	float t_pixel = 3264;
	float f_length = 3.04;
	float m_pixel = t_pixel / f_length;
	float cvt_pixel;

	float s_size;
	float r_size;

	r_size = size;

	v = array_cpt[1].x - array_cpt[0].x;

	cvt_pixel = m_pixel * img.cols * 2 /t_pixel;

	s_size = v / cvt_pixel;

	float distance = ((r_size * f_length) / s_size) / 10;
	
	cout<<distance<<" cm"<<endl;

	sprintf(dist_msg,"%.2f cm",distance);	

	return distance;
}

int hsv_handler(Mat &img)
{
	Mat img_for_detect[2];
	int red_positive, green_positive;

	green_positive=green_detect(img);
	red_positive=red_detect(img);

	if(red_positive <= 2 && green_positive > 1)
		return 2;
	else if(red_positive > 1 && green_positive < 2)
		return 1;
	else
		return 0;
}

int red_detect(Mat &img_for_detect)
{
	Mat img_binary, img_hsv;

	cvtColor(img_for_detect, img_hsv, COLOR_BGR2HSV); 
	inRange(img_hsv, Scalar(HSV_CONST::RED_HL,HSV_CONST::RED_SL,HSV_CONST::RED_VL),\
		Scalar(HSV_CONST::RED_HH,HSV_CONST::RED_SH,HSV_CONST::RED_VH), img_binary); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );

	Mat img_labels, stats, centroids;  
	int numOfLables = connectedComponentsWithStats(img_binary, img_labels, stats, centroids, 8, CV_32S);  

	//cout<<"detected reds : "<<numOfLables-1<<endl;

	return numOfLables;
}

int green_detect(Mat &img_for_detect)
{
	Mat img_binary, img_hsv;

	cvtColor(img_for_detect, img_hsv, COLOR_BGR2HSV); 
	inRange(img_hsv, Scalar(HSV_CONST::GREEN_HL,HSV_CONST::GREEN_SL,HSV_CONST::GREEN_VL),\
		Scalar(HSV_CONST::GREEN_HH,HSV_CONST::GREEN_SH,HSV_CONST::GREEN_VH), img_binary); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	dilate(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) ); 
	erode(img_binary, img_binary, getStructuringElement(MORPH_ELLIPSE, Size(5, 5)) );

	Mat img_labels, stats, centroids;  
	int numOfLables = connectedComponentsWithStats(img_binary, img_labels, stats, centroids, 4, CV_32S);  

	//cout<<"detected greens : "<<numOfLables-1<<endl;

	return numOfLables;
}
