/***************************************************************************
                          QDrawWidget.cpp  -  description
                             -------------------
    begin                : Fri Nov 17 2000
    copyright            : (C) 2000 by Rainer Lehrig
    email                : lehrig@t-online.de
 ***************************************************************************/

/***************************************************************************
 *                                                                         *
 *   This program is free software; you can redistribute it and/or modify  *
 *   it under the terms of the GNU General Public License as published by  *
 *   the Free Software Foundation; either version 2 of the License, or     *
 *   (at your option) any later version.                                   *
 *                                                                         *
 ***************************************************************************/
#include "pvdefine.h"
#include "opt.h"
#include "qdrawwidget.h"
#include "tcputil.h"

extern OPT opt;

static int mystrlen(const char *name)
{
  if(name == NULL) return 0;
  const char *cptr = strchr(name,'\"');
  if(cptr == NULL) return strlen(name);
  else return cptr - name;
}

static int mystrncmp(const char *name1, const char *name2)
{
  if(name1 == NULL || name2 == NULL) return 1;
  int len1 = mystrlen(name1);
  int len2 = mystrlen(name2);
  if(len1 != len2) return 1;
  else return strncmp(name1,name2,len1);
}

QDrawWidget::QDrawWidget( QWidget *parent, const char *name, int wFlags, int *sock, int ident)
            : QWidget( parent)
{
  if(opt.arg_debug) printf("QDrawWidget::QDrawWidget\n");
  if(name != NULL) setObjectName(name);
  fp   = NULL;
  flog = NULL;
  s  = sock;
  id = ident;
  hasLayout = 1;
  zoomx = zoomy = (float) opt.zoom / 100.0f;
  linestyle = LINESTYLE_NONE;
  boxx = boxy = boxw = boxh = 0;
  xmin = 0.0f;
  xmax = 100.0f;
  ymin = 0.0f;
  ymax = 100.0f;
  dx = dy = 1.0f;
  br = 255;
  bg = 255;
  bb = 255;
  sr = 3;
  xold = yold = 0;
  autoZoomX = autoZoomY = 1;
  origwidth = origheight = 0;
  pressedX = pressedY = -1;
  svgAnimator = NULL;
  selectorState = 0;
  QCursor *cursor = QApplication::overrideCursor();
  if(cursor == 0) originalCursor = (int) Qt::ArrowCursor;
  else            originalCursor = (int) cursor->shape();
  setMouseTracking(true);

  //qt3 setBackgroundMode(Qt::NoBackground);
  //setAutoFillBackground(false);
  strcpy(floatFormat,"%f");
  buffer = new QPixmap;
  buffer->fill(QColor(br,bg,bb));
  if(wFlags == -1000) return; //trollmurx
}

QDrawWidget::~QDrawWidget()
{
  if(fp != NULL) fclose(fp);
  if(svgAnimator != NULL) delete svgAnimator;
  delete buffer;
}

void QDrawWidget::layoutResize(int w, int h)
{
  char buf[80];
  float zxx,zyy;
  if(hasLayout == 1 && origwidth  != 0)
  {
    zxx = ((float) w / (float) origwidth) * ((float) opt.zoom / 100.0f);;
    setZoomX(zxx);
  }
  if(hasLayout == 1 && origheight != 0)
  {
    zyy = ((float) h / (float) origheight) * ((float) opt.zoom / 100.0f);
    setZoomY(zyy);
  }
  if(svgAnimator != NULL)
  {
    beginDraw();
    svgAnimator->update(0);
    endDraw();
  }
  // xlehrig
  sprintf(buf,"resizeGL(%d,%d,%d)\n",id,w,h);
  tcp_send(s,buf,strlen(buf));
  repaint();
}

void QDrawWidget::resize(int w, int h)
{
  //QWidget::resize(w,h);
  if(opt.arg_debug) printf("QDrawWidget::resize(%d,%d)\n",w,h);
  delete buffer;
  buffer = new QPixmap(w,h);
  buffer->fill(QColor(br,bg,bb));
}

void QDrawWidget::resizeEvent(QResizeEvent *event)
{
  if(opt.arg_debug) printf("QDrawWidget::resizeEvent\n");
  resize(event->size().width(),event->size().height());
  layoutResize(event->size().width(),event->size().height());
}

void QDrawWidget::setGeometry(int x, int y, int w, int h)
{
  if(opt.arg_debug) printf("QDrawWidget::setGeometry(%d,%d,%d,%d)\n",x,y,w,h);
  origwidth  = w;
  origheight = h;
  QWidget::setGeometry(x,y,w,h);
}

void QDrawWidget::setGeometry(const QRect &r)
{
  setGeometry(r.left(),r.top(),r.width(),r.height());
}

void QDrawWidget::setBackgroundColor(int r, int g, int b)
{
  br = r;
  bg = g;
  bb = b;
  buffer->fill(QColor(r,g,b));
}

void QDrawWidget::beginDraw()
{
  if(opt.arg_debug) printf("QDrawWidget::beginDraw\n");
  p.begin(buffer);
  buffer->fill(QColor(br,bg,bb));
  fontsize = p.fontInfo().pointSize();
  fontsize = (zx(fontsize)+zy(fontsize)) / 2;
}

void QDrawWidget::endDraw()
{
  if(opt.arg_debug) printf("QDrawWidget::endDraw\n");
  if(fp != NULL)
  {
    p.end();
    fclose(fp);
    fp = NULL;
    return;
  }
  p.end();
  repaint();
}

void QDrawWidget::showFromSocket(int *sock)
{
  if(sock == NULL) return;
  s = sock;
  beginDraw();
  while(interpretall() != -1) ;
  //*s = -1;
}

void QDrawWidget::showFromFile(const char *filename)
{
  fp = fopen(filename,"r");
  if(fp == NULL) return;
  beginDraw();
  while(interpretall() != -1) ;
}

void QDrawWidget::print()
{
  QPrinter printer;
  //qt3 if(printer.setup(this))
  QPrintDialog printDialog(&printer);
  if(printDialog.exec() == QDialog::Accepted)
  {
    QPainter painter;
    painter.begin(&printer);
    painter.drawPixmap(0,0,*buffer);
    painter.end();
  };
}

void QDrawWidget::logBmpToFile(const char *filename)
{
  buffer->toImage().save(filename);
}

void QDrawWidget::logToFile(const char *filename)
{
  if(flog != NULL) return;
  flog = fopen(filename,"w");
  if(flog != NULL)
  {
    fprintf(flog,"pvMetafile(%d,%d)\n",width(),height());
  }
}

void QDrawWidget::getDimensions(const char *filename, int *width, int *height)
{
  FILE *f;
  char line[1024],*cptr;

  *width = *height = 0;
  f = fopen(filename,"r");
  if(f == NULL) return;
  cptr = fgets(line,sizeof(line)-1,f);
  if(cptr != NULL && strncmp(line,"pvMetafile",10) == 0)
  {
    sscanf(line,"pvMetafile(%d,%d)",width,height);
  }
  fclose(f);
}

void QDrawWidget::copyToClipboard()
{
  QApplication::clipboard()->setPixmap(*buffer);
}

void QDrawWidget::moveTo(int x, int y)
{
  xold = zx(x);
  yold = zy(y);
  //p.moveTo(zx(x),zy(y));
}

void QDrawWidget::lineTo(int x, int y)
{
  int xnew, ynew;
  xnew = zx(x);
  ynew = zy(y);
  p.drawLine(xold,yold,xnew,ynew);
  xold = xnew;
  yold = ynew;
  //p.lineTo(zx(x),zy(y));
}

void QDrawWidget::textInAxis(float x, float y, int alignment, const char *txt)
{
  float dy;
/*
  ymax-ymin        dy
  -----------   =  ----------
  boxh             fontsize/2
*/
  if(alignment == ALIGN_CENTER)
  {
    dy = ((ymax-ymin) * (fontsize/2) ) / (boxh);
    text(tx(x), ty(y+dy), alignment, txt);
  }
  else
  {
    text(tx(x), ty(y), alignment, txt);
  }
}

void QDrawWidget::text(int x, int y, int alignment, const char *txt)
{
int len;
QString qtxt;

  qtxt = QString::fromUtf8(txt);
  len = qtxt.length();
  switch(alignment)
  {
    case ALIGN_LEFT:     //  |example
      p.drawText(zx(x),zy(y-fontsize/2),zx(len*fontsize),(zx(fontsize)+zy(fontsize)),Qt::AlignLeft,qtxt);
      break;
    case ALIGN_CENTER:   //  exa|mple
      p.drawText(zx(x-(len*fontsize)/2),zy(y-fontsize/2),zx(len*fontsize),(zx(fontsize)+zy(fontsize)),Qt::AlignCenter,qtxt);
      break;
    case ALIGN_RIGHT:    //  example|
      p.drawText(zx(x-len*fontsize),zy(y-fontsize/2),zx(len*fontsize),(zx(fontsize)+zy(fontsize)),Qt::AlignRight,qtxt);
      break;
    case ALIGN_VERT_CENTER:
      {
//#ifndef PVUNIX
        QMatrix m;
        QMatrix mident;
        m.translate(zx(x),zy(y));
        m.rotate(-90.0F);

        //qt3 p.setWorldMatrix(m);
        p.setMatrix(m);
        p.drawText(-(len*fontsize*29)/100,-fontsize/2,zx(len*fontsize),(zx(fontsize)+zy(fontsize)),Qt::AlignLeft,qtxt);
        //qt3 p.setWorldMatrix(mident);
        p.setMatrix(mident);
/*
#else
        int i,dy;
        dy = y-(len*((12*fontsize)/10))/2;
        for(i=0; i<len; i++)
        {
          p.drawText(zx(x),zy(dy),zx(fontsize*2),zy(fontsize*2),AlignLeft,&qtxt[i],1);
          dy += (12*fontsize)/10;
        }
#endif
*/
      }
      break;
    default:
      break;
  }
}

void QDrawWidget::box(int x, int y, int w, int h)
{
  boxx = x;
  boxy = y;
  boxw = w;
  boxh = h;
  moveTo(x  ,y  );
  lineTo(x+w,y  );
  lineTo(x+w,y+h);
  lineTo(x  ,y+h);
  lineTo(x  ,y  );
}

void QDrawWidget::rect(int x, int y, int w, int h)
{
  p.drawRect(zx(x),zy(y),zx(w),zy(h));
}

static int beautyval(char *text)
{
  int i,val;

  i = strlen(text);
  if(i>0)
  {
    if(text[i-1] == '9' && i>2 && text[i-2] == '9' && strchr(text,'.') != NULL ) // round up
    {
      i--;

      while(i>0)
      {
        if     (text[i] == '9') text[i] = '\0';
        else if(text[i] == '.')
        {
          sscanf(text,"%d",&val);
          val += 1;
          sprintf(&text[i-1],"%d",val);
          return 0;
        }
        else
        {
          sscanf(&text[i],"%d",&val);
          val += 1;
          sprintf(&text[i],"%d",val);



          return 0;
        }
      }
      return 0;
    }
    else if(text[i-1] == '1' && i>2 && text[i-2] == '0' && strchr(text,'.') != NULL ) // round down
    {
      i--;
      text[i] = '\0';
      i--;
      while(i>0)
      {
        if     (text[i] == '0') text[i] = '\0';
        else if(text[i] == '.')
        {
          text[i] = '\0';
          return 0;
        }
        else
        {
          return 0;
        }
      }
      return 0;
    }
    
    while(i>0)
    {
      i--;
      if(text[i] == '0') text[i] = '\0';
      else if(text[i] == '.')
      {
        text[i] = '\0';
        return 0;
      }
      else return 0;
    }
  }
  return 0;
}

void QDrawWidget::xGrid()
{
  for(float x=(xmin+dx); x<xmax; x+=dx)
  {
    xold = zx(tx(x));
    yold = zy(boxy+boxh);
    p.drawLine(zx(tx(x)),zy(boxy),xold,yold);
    //p.moveTo(zx(tx(x)),zy(boxy));
    //p.lineTo(zx(tx(x)),zy(boxy+boxh));
  }
}

void QDrawWidget::yGrid()
{
  for(float y=(ymin+dy); y<=(ymax-dy); y+=dy)
  {
    xold = zx(boxx+boxw);
    yold = zy(ty(y));
    p.drawLine(zx(boxx),zy(ty(y)),xold,yold);
    //p.moveTo(zx(boxx),     zy(ty(y)));
    //p.lineTo(zx(boxx+boxw),zy(ty(y)));
  }
}

void QDrawWidget::xAxis(float start, float delta, float end, int draw)
{
  float x;
  int   y;
  char  txt[80];

  xmin = start;
  dx   = delta;
  xmax = end;
  if(draw==0) return;

  x = start;
  y = boxy + boxh;

  while(x <= end)
  {
    moveTo(tx(x),y);
    lineTo(tx(x),y+4);
    sprintf(txt,floatFormat,x);
    if(floatFormat[1] == 'f') beautyval(txt);
    text(tx(x),y+8,ALIGN_CENTER,txt);
    x += delta;
  }

  if(start < 0.0f && end > 0.0f)
  {
    moveTo(tx(0.0f),boxy);
    lineTo(tx(0.0f),boxy+boxh);
  }
}

void QDrawWidget::yAxis(float start, float delta, float end, int draw)
{

float y;
int   x;
char  txt[80];

  ymin = start;
  dy   = delta;
  ymax = end;
  if(draw==0) return;
  x = boxx;
  y = start;

  while(y <= end)
  {
    moveTo(x,ty(y));
    lineTo(x-4,ty(y));
    sprintf(txt,floatFormat,y);
    if(floatFormat[1] == 'f') beautyval(txt);
    text(x-8,ty(y),ALIGN_RIGHT,txt);
    y += delta;
  }


  if(start < 0.0f && end > 0.0f)
  {
    moveTo(boxx,ty(0.0f));
    lineTo(boxx+boxw,ty(0.0f));
  }
}

void QDrawWidget::rightYAxis(float start, float delta, float end, int draw)
{
float y;
int   x;
char  txt[80];

  ymin = start;
  dy   = delta;
  ymax = end;
  if(draw==0) return;
  x = boxx+boxw;
  y = start;

  while(y <= end)
  {
    xold = x+4;
    yold = ty(y);
    p.drawLine(x,ty(y),xold,yold);
    //p.moveTo(x,ty(y));
    //p.lineTo(x+4,ty(y));
    sprintf(txt,floatFormat,y);
    if(floatFormat[1] == 'f') beautyval(txt);
    text(x+8,ty(y),ALIGN_LEFT,txt);
    y += delta;
  }

  if(start < 0.0f && end > 0.0f)
  {
    int xnew, ynew;
    xold = boxx+boxw;
    yold = ty(0.0f);
    xnew = boxx+boxw;
    ynew = ty(0.0f);
    p.drawLine(xold,yold,xnew,ynew);
    xold = xnew;
    yold = ynew;
    //p.moveTo(boxx,ty(0.0f));
    //p.lineTo(boxx+boxw,ty(0.0f));
  }
}

void QDrawWidget::drawSymbol(int x, int y, int type)
{
  int xnew, ynew;
  switch(type)
  {
    case LINESTYLE_CIRCLE:
      p.drawArc(zx(x-sr),zy(y-sr),zx(2*sr),zy(2*sr),0,360);
      break;

    case LINESTYLE_CROSS:
      xold = zx(x+sr);
      yold = zy(y-sr);
      p.drawLine(zx(x-sr),zy(y+sr),xold,yold);
      xold = zx(x+sr);
      yold = zy(y+sr);
      p.drawLine(zx(x-sr),zy(y-sr),xold,yold);
      //p.moveTo(zx(x-sr),zy(y+sr));
      //p.lineTo(zx(x+sr),zy(y-sr));
      //p.moveTo(zx(x-sr),zy(y-sr));
      //p.lineTo(zx(x+sr),zy(y+sr));
      break;
    case LINESTYLE_RECT:
      xold = zx(x+sr);
      yold = zy(y-sr);
      p.drawLine(zx(x-sr),zy(y-sr),xold,yold);
      xnew = zx(x+sr);
      ynew = zy(y+sr);
      p.drawLine(xold,yold,xnew,ynew);
      xold = xnew;
      yold = ynew;
      xnew = zx(x-sr);
      ynew = zy(y+sr);
      p.drawLine(xold,yold,xnew,ynew);
      xold = xnew;
      yold = ynew;
      xnew = zx(x-sr);
      ynew = zy(y-sr);
      p.drawLine(xold,yold,xnew,ynew);
      xold = xnew;
      yold = ynew;
      //p.moveTo(zx(x-sr),zy(y-sr));
      //p.lineTo(zx(x+sr),zy(y-sr));
      //p.lineTo(zx(x+sr),zy(y+sr));
      //p.lineTo(zx(x-sr),zy(y+sr));
      //p.lineTo(zx(x-sr),zy(y-sr));
      break;
    default:
      break;
  }
}

void QDrawWidget::line(float *x, float *y, int n)
{
  moveTo(tx(x[0]),ty(y[0]));
  for(int i=1; i<n; i++) lineTo(tx(x[i]),ty(y[i]));
  if(linestyle != LINESTYLE_NONE)
  {
    for(int i=1; i<n; i++) drawSymbol(tx(x[i]),ty(y[i]),linestyle);
  }
}

void QDrawWidget::setColor(int r, int g, int b)
{
  p.setBrush(QColor(r,g,b));
  //p.setPen(QColor(r,g,b));
  QPen pen;
  pen = p.pen();
  pen.setColor(QColor(r,g,b));
  p.setPen(pen);
}

void QDrawWidget::setFont(const char *family, int size, int weight, int italic)
{
  size = (int) (((size * zoomx) + (size * zoomy)) / 2);
  fontsize = size;
  p.setFont(QFont(family,size,weight,italic));
}

void QDrawWidget::setLinestyle(int style)
{
  linestyle = style;
}

void QDrawWidget::paintEvent(QPaintEvent *e)
{
  // if(e != NULL) bitBlt(this,0,0,&buffer);
  if(e == NULL) return;
  //printf("QDrawWidget::paintEvent()\n");
  QPainter painter;
  painter.begin(this);
  painter.drawPixmap(0,0,*buffer);
  if(pressedX != -1)
  {
    int w = movedX - pressedX;
    int h = movedY - pressedY;
    painter.setPen(QColor(128,128,128));
    if(selectorState == 1)
    {
      if     (w>0 && h>0) painter.drawRect(pressedX,pressedY,w,h);
      else if(w<0 && h>0) painter.drawRect(movedX,pressedY,-w,h);
      else if(w>0 && h<0) painter.drawRect(pressedX,movedY,w,-h);
      else if(w<0 && h<0) painter.drawRect(movedX,movedY,-w,-h);
    }
  }
  painter.end();
}

void QDrawWidget::mouseMoveEvent(QMouseEvent *event)
{
  int w,h;
  char buf[100];

  movedX = event->x();
  movedY = event->y();
  int buttons = event->buttons();
  if(buttons == Qt::LeftButton || buttons == Qt::RightButton || buttons == Qt::MidButton)
  {
    sprintf( buf, "QPlotMouseMoved(%d,%d,%d)\n",id, movedX, movedY);
    tcp_send(s,buf,strlen(buf));
  }  
  QWidget::mouseMoveEvent(event);
  w = movedX - pressedX;
  h = movedY - pressedY;
#if QT_VERSION >= 0x040201
  if(svgAnimator != NULL)
  {
    svgAnimator->perhapsSetOverrideCursor(movedX,movedY,zoomx,zoomy,buttons);
  }  
#endif
  if(selectorState == 0) return;
  repaint();
}

void QDrawWidget::mousePressEvent(QMouseEvent *event)
{
  char buf[100];
  int  button;

  pressedX = event->x();
  pressedY = event->y();
  QWidget::mousePressEvent(event);
  if(svgAnimator != NULL)
  {
    button = event->button();
    if(button == Qt::LeftButton)  svgAnimator->perhapsSendSvgEvent("svgPressedLeftButton" ,s,id,pressedX,pressedY,zoomx,zoomy);
    if(button == Qt::RightButton) svgAnimator->perhapsSendSvgEvent("svgPressedRightButton",s,id,pressedX,pressedY,zoomx,zoomy);
    if(button == Qt::MidButton)   svgAnimator->perhapsSendSvgEvent("svgPressedMidButton"  ,s,id,pressedX,pressedY,zoomx,zoomy);
  }
  sprintf( buf, "QPlotMousePressed(%d,%d,%d)\n",id, pressedX, pressedY);
  tcp_send(s,buf,strlen(buf));
}

void QDrawWidget::mouseReleaseEvent(QMouseEvent *event)
{
  char buf[100];
  int  button;

  pressedX = -1;
  pressedY = -1;
  movedX   = event->x();
  movedY   = event->y();
  if(svgAnimator != NULL)
  {
    button = event->button();
    if(button == Qt::LeftButton)  svgAnimator->perhapsSendSvgEvent("svgReleasedLeftButton" ,s,id,movedX,movedY,zoomx,zoomy);
    if(button == Qt::RightButton) svgAnimator->perhapsSendSvgEvent("svgReleasedRightButton",s,id,movedX,movedY,zoomx,zoomy);
    if(button == Qt::MidButton)   svgAnimator->perhapsSendSvgEvent("svgReleasedMidButton"  ,s,id,movedX,movedY,zoomx,zoomy);
  }
  sprintf( buf, "QPlotMouseReleased(%d,%d,%d)\n",id, movedX, movedY);
  if(underMouse()) tcp_send(s,buf,strlen(buf));
  QWidget::mouseReleaseEvent(event);
  if(selectorState == 0) return;
  if(event == NULL) return;
  repaint();
}

void QDrawWidget::enterEvent(QEvent *event)
{
  char buf[100];
  sprintf(buf, "mouseEnterLeave(%d,1)\n",id);
  tcp_send(s,buf,strlen(buf));
  QCursor *cursor = QApplication::overrideCursor();
  if(cursor == 0) originalCursor = (int) Qt::ArrowCursor;
  else            originalCursor = (int) cursor->shape();
  QWidget::enterEvent(event);
}

void QDrawWidget::leaveEvent(QEvent *event)
{
  char buf[100];
  sprintf(buf, "mouseEnterLeave(%d,0)\n",id);
  tcp_send(s,buf,strlen(buf));
  QApplication::setOverrideCursor(QCursor((Qt::CursorShape) originalCursor));
  QWidget::leaveEvent(event);
}

void QDrawWidget::setZoomX(float zx)
{
  if(zx >= 0.0f)
  {
    if(autoZoomX) zoomx = zx;
  }
  else
  {
    autoZoomX = 0;
    zoomx = -zx;
  }
}

void QDrawWidget::setZoomY(float zy)
{
  if(zy >= 0.0f)
  {
    if(autoZoomY) zoomy = zy;
  }
  else
  {
    autoZoomY = 0;
    zoomy = -zy;
  }
}

int QDrawWidget::tx(float x)
{
  return (int)(boxx + (((x-xmin)*boxw)/(xmax-xmin)));    // x          i
}                                                        // xmax-xmin  boxw

int QDrawWidget::ty(float y)
{
  int i0;
  i0 = (int)((ymin * boxh)/(ymax-ymin));
  return i0 + (int)(boxy+boxh-((y*boxh)/(ymax-ymin))); // y          i        ymin       i0
}                                                      // ymax-ymin  boxh     ymax-ymin  boxh

int QDrawWidget::zx(int x)
{
  return (int)(zoomx * ((float) x) );
}

int QDrawWidget::zy(int y)
{
  return (int)(zoomy * ((float) y) );
}

void QDrawWidget::playSVG(const char *filename)
{
  char buf[MAXARRAY+1],*cptr;
  QByteArray stream;
  FILE *fin;
  int foundw,foundh,foundclose;

  if(opt.arg_debug) printf("Qt4 playSVG(%s)\n",filename);
  fin = fopen(filename,"r");
  if(fin == NULL) return;
  foundw = foundh = foundclose = 0;
  while(fgets(buf,sizeof(buf)-1,fin) != NULL)
  {
    if(opt.arg_debug) printf("PlaySVG::buf=%s",buf);
    if(foundw == 0 && (cptr = strstr(buf," width=")) != NULL)
    {
      if(strstr(buf,">") != NULL) foundclose = 1;
      sprintf(buf,"width=\"%dpx\"\n",width());
      if(foundclose) strcat(buf,">\n");
      foundw = 1;
    }
    if(foundh == 0 && (cptr = strstr(buf," height=")) != NULL)
    {
      if(strstr(buf,">") != NULL) foundclose = 1;
      sprintf(buf,"height=\"%dpx\"\n",height());
      if(foundclose) strcat(buf,">\n");
      foundh = 1;
    }
    stream.append(QString::fromUtf8(buf));
    if(opt.arg_debug > 2) printf("svgbuf=%s",buf);
  }
  fclose(fin);

  //renderer.setViewBox( QRect(0,0,width(),height()) );
  renderer.load(stream);
  p.scale(zoomx,zoomy);
  renderer.render(&p);
  p.scale(1.0,1.0);
}

void QDrawWidget::socketPlaySVG()
{
  char buf[MAXARRAY+1];
  int ret,foundw,foundh;
  QByteArray stream;

  if(opt.arg_debug) printf("Qt4 socketPlaySVG\n");
  if(s == NULL) return;
  foundw = foundh = 0;
  while(*s != -1)
  {
    ret = tcp_rec(s,buf,sizeof(buf)-1);
    if(ret < 0) return;
    if(foundw == 0 && strncmp(buf,"width",5) == 0)
    {
      sprintf(buf,"width=\"%dpx\"\n",width());
      foundw = 1;
    }
    if(foundh == 0 && strncmp(buf,"height",6) == 0)
    {
      sprintf(buf,"height=\"%dpx\"\n",height());
      foundh = 1;
    }
    if(strstr(buf,"<svgend></svgend>") != NULL) break; 
    stream.append(QString::fromUtf8(buf));
    if(opt.arg_debug > 2) printf("svgbuf=%s",buf);
  }
  renderer.load(stream);
  p.scale(zoomx,zoomy);
  renderer.render(&p);
  p.scale(1.0,1.0);
  if(opt.arg_debug) printf("Qt4 socketPlaySVG end\n");
}

int QDrawWidget::getText(const char *line, char *text)
{
  *text = '\0';
  while(*line != '"' && *line >= ' ') line++;
  if(*line < ' ') return 0;
  line++;
  while(*line != '"' && *line != '\0') *text++ = *line++;
  *text = '\0';
  return 0;
}

int QDrawWidget::gets(char *line, int len)
{
  if(s != NULL)
  {
    if(tcp_rec(s,line,len) == -1) return -1;
    if(flog != NULL)
    {
      fputs(line,flog);
      if(strncmp(line,"gendDraw",8) == 0)
      {
        fclose(flog);
        flog = NULL;
      }
    }
    return 0;
  }
  if(fp != NULL)
  {
    if(fgets(line,len,fp) == NULL) return -1;
    return 0;
  }
  return -1;
}

int QDrawWidget::interpretall()
{
char linebuf[MAXARRAY];

  if(gets(linebuf,sizeof(linebuf)-1) == -1) { endDraw(); return -1; }
  return interpret(linebuf);

}

int QDrawWidget::interpret(const char *linebuf)
{
int x,y,w,h,r,g,b,n,i;

  if(opt.arg_debug > 1) printf("QDrawWidget=%s",linebuf);
  switch(linebuf[1])
  {
    case 'b':
      if(strncmp(linebuf,"gbeginDraw",10) == 0)
      {
        strcpy(floatFormat,"%f");
        beginDraw();
      }
      else if(strncmp(linebuf,"gbox",4) == 0)
      {
        sscanf(linebuf,"gbox(%d,%d,%d,%d)",&x,&y,&w,&h);
        box(x,y,w,h);
      }
      break;
    case 'd':
      if(strncmp(linebuf,"gdrawArc",8) == 0)
      {
        int astart,alen;
        sscanf(linebuf,"gdrawArc(%d,%d,%d,%d,%d,%d)",&x,&y,&w,&h,&astart,&alen);
        p.drawArc(zx(x),zy(y),zx(w),zy(h),astart*16,alen*16);
      }
      else if(strncmp(linebuf,"gdrawPie",8) == 0)
      {
        int astart,alen;
        sscanf(linebuf,"gdrawPie(%d,%d,%d,%d,%d,%d)",&x,&y,&w,&h,&astart,&alen);
        p.drawPie(zx(x),zy(y),zx(w),zy(h),astart*16,alen*16);
      }
      else if(strncmp(linebuf,"gdrawPolygon",12) == 0)
      {
        char buf[80];
        int cnt;
        sscanf(linebuf,"gdrawPolygon(%d)",&cnt);
        QPolygon points(cnt);
        i=0;
        while(1)
        {
          gets(buf,sizeof(buf)-1);
          if(buf[0] == '}') break;
          sscanf(buf,"%d,%d",&x,&y);
          points.setPoint(i++,zx(x),zy(y));
        }
        p.drawPolygon(points);
      }
      break;
    case 'e':
      if(strncmp(linebuf,"gendDraw",8) == 0)
      {
        strcpy(floatFormat,"%f");
        endDraw();
        return -1;
      }
      break;
    case 'l':
      if(strncmp(linebuf,"glineTo",7) == 0)
      {
        sscanf(linebuf,"glineTo(%d,%d)",&x,&y);
        lineTo(x,y);
      }
      else if(strncmp(linebuf,"gline(",6) == 0)
      {
        int maxarray = MAXARRAY;
        const char *cptr;
        cptr = strstr(linebuf,"-n=");
        if(cptr != NULL) sscanf(cptr,"-n=%d", &maxarray);
        if(maxarray < 1) maxarray = MAXARRAY;
        float xa[maxarray],ya[maxarray];
        char buf[80];
        for(n=0; 1; n++)
        {
          if(gets(buf,sizeof(buf)-1) == -1) break;
          if(buf[0] == '}') break;
          if(n >= maxarray) break;
          sscanf(buf,"%f,%f",&xa[n],&ya[n]);
        }
        if(n>0) line(xa,ya,n);
      }
      break;
    case 'm':
      if(strncmp(linebuf,"gmoveTo",7) == 0)
      {
        sscanf(linebuf,"gmoveTo(%d,%d)",&x,&y);
        moveTo(x,y);
      }
      break;
    case 'p':
      if(strncmp(linebuf,"gplaySVGsocket",14) == 0)
      {
        socketPlaySVG();
      }
      else if(strncmp(linebuf,"gplaySVG",8) == 0)
      {
        char filename[1024];
        getText(linebuf,filename);
        playSVG(filename);
      }
      break;
    case 'r':
      if(strncmp(linebuf,"grect",5) == 0)
      {
        sscanf(linebuf,"grect(%d,%d,%d,%d)",&x,&y,&w,&h);
        rect(x,y,w,h);
      }
      else if(strncmp(linebuf,"grightYAxis",11) == 0)
      {
        float start,delta,end;
        int draw;
        sscanf(linebuf,"grightYAxis(%f,%f,%f,%d)",&start,&delta,&end,&draw);
        rightYAxis(start,delta,end,draw);
      }
      else if(strncmp(linebuf,"grotate",7) == 0)
      {
        float rx;
        sscanf(linebuf,"grotate(%f)",&rx);
        p.rotate(rx);
      }
      break;
    case 's':
      if(strncmp(linebuf,"gsetBackgroundColor",19) == 0)
      {
        sscanf(linebuf,"gsetBackgroundColor(%d,%d,%d)",&r,&g,&b);
        setBackgroundColor(r,g,b);
      }
      else if(strncmp(linebuf,"gsetColor",9) == 0)
      {
        sscanf(linebuf,"gsetColor(%d,%d,%d)",&r,&g,&b);
        setColor(r,g,b);
      }
      else if(strncmp(linebuf,"gsetWidth",9) == 0)
      {
        QPen pen;
        sscanf(linebuf,"gsetWidth(%d)",&w);
        pen = p.pen();
        pen.setWidth(w);
        p.setPen(pen);
      }
      else if(strncmp(linebuf,"gsetStyle",9) == 0)
      {
        QPen pen;
        sscanf(linebuf,"gsetStyle(%d)",&i);
        pen = p.pen();
        if((Qt::PenStyle) i == Qt::NoPen) i = 1;
        pen.setStyle((Qt::PenStyle) i);
        //if(i==0) pen.setStyle(Qt::SolidLine);
        //if(i==1) pen.setStyle(Qt::DotLine);
        p.setPen(pen);
      }
      else if(strncmp(linebuf,"gsetFont",8) == 0)
      {
        char family[80];
        const char *cptr;
        int  size,weight,italic;
        getText(linebuf,family);
        cptr = strstr(linebuf,"\",");
        if(cptr != NULL)
        {
          cptr += 2;
          sscanf(cptr,"%d,%d,%d",&size,&weight,&italic);
          setFont(family,size,weight,italic);
        }
      }
      else if(strncmp(linebuf,"gsetLinestyle",13) == 0)
      {
        sscanf(linebuf,"gsetLinestyle(%d)",&i);
        setLinestyle(i);
      }
      else if(strncmp(linebuf,"gsetFloatFormat",15) == 0)
      {
        char txt[1024];
        getText(linebuf,txt);
        strcpy(floatFormat,txt);
      }
      else if(strncmp(linebuf,"gscale",6) == 0)
      {
        float rx,ry;
        sscanf(linebuf,"gscale(%f,%f)",&rx,&ry);
        p.scale(rx,ry);
      }
      break;
    case 't':
      if(strncmp(linebuf,"gtext(",6) == 0)
      {
        char txt[1024];
        int alignment;
        sscanf(linebuf,"gtext(%d,%d,%d,",&x,&y,&alignment);
        getText(linebuf,txt);
        text(x,y,alignment,txt);
      }
      else if(strncmp(linebuf,"gtextInAxis(",12) == 0)
      {
        char txt[1024];
        int alignment;
        float fx,fy;
        sscanf(linebuf,"gtextInAxis(%f,%f,%d,",&fx,&fy,&alignment);
        getText(linebuf,txt);
        textInAxis(fx,fy,alignment,txt);
      }
      else if(strncmp(linebuf,"gtranslate",10) == 0)
      {
        float rx,ry;
        sscanf(linebuf,"gtranslate(%f,%f)",&rx,&ry);
        p.translate(rx,ry);
      }
      break;
    case 'u':
      if(strncmp(linebuf,"gupdateSVG(",11) == 0)
      {
        if(svgAnimator != NULL) svgAnimator->update(0);
      }
      break;
    case 'x':
      if(strncmp(linebuf,"gxAxis",6) == 0)
      {
        float start,delta,end;
        int draw;
        sscanf(linebuf,"gxAxis(%f,%f,%f,%d)",&start,&delta,&end,&draw);
        xAxis(start,delta,end,draw);
      }
      else if(strncmp(linebuf,"gxGrid",6) == 0)
      {
        xGrid();
      }
      break;
    case 'y':
      if(strncmp(linebuf,"gyAxis",6) == 0)
      {
        float start,delta,end;
        int draw;
        sscanf(linebuf,"gyAxis(%f,%f,%f,%d)",&start,&delta,&end,&draw);
        yAxis(start,delta,end,draw);
      }
      else if(strncmp(linebuf,"gyGrid",6) == 0)
      {
        yGrid();
      }
      break;
  }
  return 0;
}

void QDrawWidget::svgUpdate(QByteArray &stream)
{
  //printf("load\n");
  renderer.load(stream);
  //printf("scale\n");
  p.scale(zoomx,zoomy);
  //printf("render\n");
  //renderer.render(&p, QRect(QPoint(0, 0), renderer.defaultSize()));
  renderer.render(&p);
  //printf("scale\n");
  p.scale(1.0,1.0);
  //printf("end\n");
}

void QDrawWidget::printSVG(QByteArray &stream)
{
  if(opt.arg_debug) printf("printSVG\n");
  QPrinter printer;
  //printer.setOrientation(QPrinter::Landscape);
  printer.setColorMode(QPrinter::Color);
  QPrintDialog printDialog(&printer, this);
  if(printDialog.exec() == QDialog::Accepted)
  {
    // print ...
    QSvgRenderer svgrenderer;
    svgrenderer.load(stream);
    QPainter painter;
    painter.begin(&printer);
    svgrenderer.render(&painter);
    painter.end();
  }
}

//#### pvSvgAnimator begin ###############################################################
pvSvgAnimator::pvSvgAnimator(int *socket, QDrawWidget *_draw)
{
  first = NULL;
  s = socket;
  draw = _draw;
  comment = NULL;
  num_lines = 0;
}

pvSvgAnimator::~pvSvgAnimator()
{
  closefile();
}

int pvSvgAnimator::closefile()
{
  SVG_LINE *current_line, *next_line;

  if(first == NULL) return -1;
  if(comment != NULL) delete [] comment;
  current_line = first;
  while(current_line != NULL)
  {
    next_line = current_line->next;
    if(current_line->line != NULL) delete [] current_line->line;
    delete current_line;
    current_line = next_line;
  }
  first = NULL;
  return 0;
}

void pvSvgAnimator::perhapsFixQtBugOnPath(SVG_LINE *next_line, const char *line)
{
  QString str;
  int i = 0;
  str.append(line[i++]); // d
  str.append(line[i++]); // =
  str.append(line[i++]); // "

  char found = '\0';
  while(line[i] != '\0')
  {
    if     (line[i] == 'm')   found = 'm';
    else if(line[i] == 'M')   found = 'M';
    else if(isalpha(line[i])) found = '\0';

    if(found != '\0')
    { 
      // copy first coordinate pair
      while(line[i] != ',' && line[i] != '\0') str.append(line[i++]);
      while(line[i] != ' ' && line[i] != '\0') str.append(line[i++]);
      // perhaps insert lineto
      while(line[i] != '\0')
      {
        if(isalpha(line[i])) break;
        if(line[i] == '-' || line[i] == '+' || isdigit(line[i]))
        {
          // we found the next coordinate pair
          if(found == 'm')
          {
            str.append('l'); str.append(' ');
          }
          else if(found == 'M')
          {
            str.append('L'); str.append(' ');
          }
          // copy this coordinate pair
          while(line[i] != ',' && line[i] != '\0') str.append(line[i++]);
          while(line[i] != ' ' && line[i] != '\0') str.append(line[i++]);
        }
        else
        {
          str.append(line[i++]);
        }
      }
    }
    else
    {
      str.append(line[i++]);
    }    
  }

  next_line->line = new char[str.length()+1];
  strcpy(next_line->line, str.toUtf8());
}

int pvSvgAnimator::read()
{
  SVG_LINE *current_line, *next_line;
  char line[MAXARRAY],*cptr;

  closefile(); // free old file

  tcp_rec(s,line,sizeof(line));
  first = new SVG_LINE;
  first->line = new char[strlen(line)+1];
  strcpy(first->line,line);
  first->next = NULL;
  current_line = first;
  num_lines = 1;
  while(*s != -1)
  {
    tcp_rec(s,line,sizeof(line));
    if(strstr(line,"<svgend></svgend>") != NULL) break;
    if(strncmp(line,"viewBox=",8) != 0)
    {
      cptr = strchr(line,'\n');
      if(cptr != NULL) *cptr = '\0';
      current_line->next = new SVG_LINE;
      next_line = current_line->next;
      if(line[0] == 'd' && line[1] == '=')
      {
        perhapsFixQtBugOnPath(next_line, line);
      }
      else
      {
        next_line->line = new char[strlen(line)+1];
        strcpy(next_line->line,line);
      }  
      next_line->next = NULL;
      current_line = next_line;
      num_lines++;
    }  
  }

  comment = new char[num_lines+1];
  comment[num_lines] = '\0';
  for(int i=0; i<num_lines; i++)
  {
    comment[i] = ' ';
  }
  return 0;
}

int pvSvgAnimator::update(int on_printer)
{
  //char buf[MAXARRAY+1];
  char *buf;
  int foundw,foundh;
  QByteArray stream;
  SVG_LINE *current_line = first;
  if(first == NULL) return -1;

  if(opt.arg_debug) printf("animatorUpdate\n");
  if(s == NULL) return -1;
  foundw = foundh = 0;
  //printf("update start\n");
  for(int i=0; i<num_lines; i++)
  {
    if(opt.arg_debug) printf("animatorUpdate line=%d text=%s\n",i,current_line->line);
    if(comment[i] == ' ' && current_line->line != NULL)
    {
      buf = new char [strlen(current_line->line) + 4];
      //if(strlen(current_line->line) < (sizeof(buf) - 2))
      //{
        strcpy(buf,current_line->line);
        if(foundw == 0 && strncmp(buf,"width",5) == 0)
        {
          sprintf(buf,"width=\"%dpx\"\n",draw->width());
          foundw = 1;
        }
        if(foundh == 0 && strncmp(buf,"height",6) == 0)
        {
          sprintf(buf,"height=\"%dpx\"\n",draw->height());
          foundh = 1;
        }
        if(buf[0] != '\n')
        {
          if(opt.arg_debug) printf("line=%s",buf);
          if(strchr(buf,'\n') == NULL) strcat(buf,"\n");
          if(opt.arg_debug) printf("end\n");
          //stream.append(QString::fromUtf8(buf));
          stream.append(buf);
          //printf("%s", buf);
        }
      //}
      //else
      //{
      //  printf("WARNING: pvSvgAnimator::update buf too small\n");
      //}
      delete [] buf;
    }
    current_line = current_line->next;
    if(current_line == NULL) break;
  }
  //printf("svgUpdate start\n");
  if(opt.arg_debug) printf("animatorUpdate svgUpdate\n");
  if(draw != NULL)
  {
    if(on_printer == 0) draw->svgUpdate(stream);
    else                draw->printSVG(stream);
  }  
  if(opt.arg_debug) printf("animatorUpdate end\n");
  //printf("update end\n");
  return 0;
}

int pvSvgAnimator::wrapTransformation(int iline, SVG_LINE *last_open, const char *objectname, const char *tag, const char *text)
{
  SVG_LINE *svgptr, *svgptrold, *svgnewptr;
  char *mycomment, commentChar;
  int iopen, i;
  char buf[1024];

  commentChar = comment[iline];
  mycomment = new char[num_lines+5+1]; // we add 5 lines of code into the SVG
  num_lines += 5;
  for(i=0; i<num_lines; i++) mycomment[i] = ' '; // mycomment is adjusted befor return

  // remember last_open->line and subst by "<g"
  strcpy(buf,last_open->line);
  delete last_open->line;
  last_open->line = new char [3];
  strcpy(last_open->line,"<g");
  svgptr = last_open;

  // insert line "id=pvb:objectname"
  svgnewptr = new SVG_LINE;
  svgnewptr->next = svgptr->next;
  svgptr->next = svgnewptr;
  svgnewptr->line = new char [strlen(objectname)+10];
  sprintf(svgnewptr->line,"id=\"pvb:%s\"",objectname);

  svgptr = svgnewptr;

  // insert line "transform=\"text\""
  svgnewptr = new SVG_LINE;
  svgnewptr->next = svgptr->next;
  svgptr->next = svgnewptr;
  svgnewptr->line = new char [strlen(tag)+strlen(text)+3];
  sprintf(svgnewptr->line,"%s\"%s\"",tag,text);

  svgptr = svgnewptr;

  // insert line ">"
  svgnewptr = new SVG_LINE;
  svgnewptr->next = svgptr->next;
  svgptr->next = svgnewptr;
  svgnewptr->line = new char [2];
  sprintf(svgnewptr->line,">");

  svgptr = svgnewptr;

  // insert line "<g"
  svgnewptr = new SVG_LINE;
  svgnewptr->next = svgptr->next;
  svgptr->next = svgnewptr;
  svgnewptr->line = new char [strlen(buf)+1];
  strcpy(svgnewptr->line,buf);

  mycomment[iline+1] = commentChar;
  mycomment[iline+2] = commentChar;
  mycomment[iline+3] = commentChar;
  mycomment[iline+4] = commentChar;

  // now insert closing "</g>"
  iopen = 0;
  svgptr = svgptrold = svgptr->next;
  while(svgptr->next != NULL)
  {
    iline++;
    if     (strncmp(svgptr->line,"</g>",4) == 0  ) iopen--;
    else if(strncmp(svgptr->line,"/>"  ,2) == 0  ) iopen--;
    else if(strncmp(svgptr->line,"</"  ,2) == 0  ) iopen--;
    else if(strncmp(svgptr->line,"<"   ,1) == 0  ) iopen++;
    //printf("iopen=%d svgptr->line=%s\n",iopen,svgptr->line);
    if(iopen <= 0)
    {
      mycomment[iline+5] = commentChar;

      svgptr = svgptrold;

      // insert line "</g>"
      svgptr = svgptr->next;
      svgnewptr = new SVG_LINE;
      svgnewptr->next = svgptr->next;
      svgptr->next = svgnewptr;
      svgnewptr->line = new char [6];
      strcpy(svgnewptr->line,"</g>");

      // debug output
      /*
      svgptr = last_open;
      while(svgptr->next != NULL)
      {
        printf("%s\n",svgptr->line);
        svgptr = svgptr->next;
      }
      */

      mycomment[num_lines] = '\0';
      delete [] comment;
      comment = mycomment;
      return 0;
    }
    svgptrold = svgptr;
    svgptr = svgptr->next;
  }

  delete [] mycomment;
  return -1;
}

int pvSvgAnimator::svgPrintf(const char *objectname, const char *tag, const char *text, const char *after)
{
  char buf[MAXARRAY+40],*cptr,*cptr2;
  SVG_LINE *current_line = first;
  SVG_LINE *last_open = NULL;
  SVG_LINE *last = NULL;
  int i,ilast;
  int len = strlen(objectname);
  if(first == NULL) return -1;

  if(opt.arg_debug) printf("svgPrintf(%s,%s,%s)\n",objectname,tag,text);
  ilast = 0;
  for(i=0; i<num_lines; i++) // find objectname
  {
    if(current_line->line[0] == '<') 
    {
      ilast = i;
      last_open = current_line;
    }
    if(strncmp(current_line->line,"id=",3) == 0)
    {
      if     (mystrncmp(&current_line->line[4],objectname) == 0) 
      { // this is a normal object
        if(strcmp(tag,"transform=") == 0) // we may have to wrap the transformation matrix to a superobject
        {
          if(opt.arg_debug) printf("wrapTransformation(%s,%s,%s)\n",objectname,tag,text);
// debugging prints commented out
/*
current_line = first;
for(int itest=0; ; itest++)
{
  printf("%c: %s\n", comment[itest], current_line->line);
  current_line = current_line->next;
  if(current_line == NULL) break;
}
*/
          wrapTransformation(ilast,last_open,objectname,tag,text);
/*
current_line = first;
for(int itest=0; ; itest++)
{
  printf("%c: %s\n", comment[itest], current_line->line);
  current_line = current_line->next;
  if(current_line == NULL) break;
}
*/
          return 0;
        }
        break; // now we have found the object -> continue
      }
      else if(strncmp(&current_line->line[4],"pvb:",4) == 0 && 
              mystrncmp(&current_line->line[8],objectname) == 0) 
      { // this is a wrapped object with a transformation matrix
        break; // now we have found the object -> continue
      }
    }
    current_line = current_line->next;
  }
  if(i >= num_lines) return -1;

  current_line = last_open;
  len = strlen(tag);
  for(i=ilast; i<num_lines; i++) // find tag
  {
    if(strncmp(current_line->line,tag,len) == 0)
    {
      break;
    }
    if(strncmp(current_line->line,"/>",2) == 0)
    {
      break;
    }
    last = current_line;
    current_line = current_line->next;
  }
  if(i >= num_lines) return -1;

  if(after == NULL) // svg printf
  {
    sprintf(buf,"%s\"",tag);
    strcat(buf,text);
    strcat(buf,"\"\n");
    //printf("svgPrintf(%s,%s,%s) old_line=%s\n",objectname,tag,text,current_line->line);
    delete [] current_line->line;
    current_line->line = new char[strlen(buf) + 1];
    strcpy(current_line->line,buf);
    //printf("svgPrintf(%s,%s,%s) new_line=%s\n",objectname,tag,text,current_line->line);
  }
  else // search and replace
  {
    sprintf(buf,"%s\"",tag);
    cptr = strchr(current_line->line,'=');
    if(cptr == NULL) return -1;
    cptr++; cptr++;
    cptr2 = strrchr(cptr,'\"');
    if(cptr2 == NULL) return -1;
    *cptr2 = '\0';
    QString qbuf(cptr);
    QRegExp rx(text,Qt::CaseSensitive,QRegExp::Wildcard);
    qbuf.replace(rx,after);
    strcat(buf,qbuf.toUtf8());
    strcat(buf,"\"\n");
    //printf("svgPrintf(%s,%s,%s) old_line=%s\n",objectname,tag,text,current_line->line);
    delete [] current_line->line;
    current_line->line = new char[strlen(buf) + 1];
    strcpy(current_line->line,buf);
    //printf("svgPrintf(%s,%s,%s) new_line=%s\n",objectname,tag,text,current_line->line);
  }
  return len;
}

int pvSvgAnimator::svgRecursivePrintf(const char *objectname, const char *tag, const char *text, const char *after)
{
  char buf[MAXARRAY+40],*cptr,*cptr2;
  SVG_LINE *current_line = first;
  SVG_LINE *last_open = NULL;
  int i,ilast,num_open;
  int len = strlen(objectname);
  if(first == NULL) return -1;

  if(opt.arg_debug) printf("svgRecursivePrintf(%s,%s,%s)\n",objectname,tag,text);
  ilast = 0;
  for(i=0; i<num_lines; i++) // find objectname
  {
    if(current_line->line[0] == '<') 
    {
      ilast = i;
      last_open = current_line;
    }
    if(strncmp(current_line->line,"id=",3) == 0)
    {
      if     (mystrncmp(&current_line->line[4],objectname) == 0) 
      { // this is a normal object
        if(strcmp(tag,"transform=") == 0) // we may have to wrap the transformation matrix to a superobject
        {
          if(opt.arg_debug) printf("wrapTransformation(%s,%s,%s)\n",objectname,tag,text);
// debugging prints commented out
/*
current_line = first;
for(int itest=0; ; itest++)
{
  printf("%c: %s\n", comment[itest], current_line->line);
  current_line = current_line->next;
  if(current_line == NULL) break;
}
*/
          wrapTransformation(ilast,last_open,objectname,tag,text);
/*
current_line = first;
for(int itest=0; ; itest++)
{
  printf("%c: %s\n", comment[itest], current_line->line);
  current_line = current_line->next;
  if(current_line == NULL) break;
}
*/
          return 0;
        }
        break; // now we have found the object -> continue
      }
      else if(strncmp(&current_line->line[4],"pvb:",4) == 0 && 
              mystrncmp(&current_line->line[8],objectname) == 0) 
      { // this is a wrapped object with a transformation matrix
        break; // now we have found the object -> continue
      }
    }
    current_line = current_line->next;
  }
  if(i >= num_lines) return -1;

  current_line = last_open;
  len = strlen(tag);
  num_open = 0;
  for( ; i<num_lines; i++) // for all childs
  {
    if(strncmp(current_line->line,"</",2) == 0)
    {
      num_open--;
    }
    else if(current_line->line[0] == '<') 
    {
      num_open++;
    }
    else if(strncmp(current_line->line,"/>",2) == 0)
    {
      num_open--;
    }
    else if(strncmp(current_line->line,tag,len) == 0)
    {
      if(after == NULL) // svg printf
      {
        sprintf(buf,"%s\"",tag);
        strcat(buf,text);
        strcat(buf,"\"\n");
        //printf("svgRecursivePrintf(%s,%s,%s) old_line=%s\n",objectname,tag,text,current_line->line);
        delete [] current_line->line;
        current_line->line = new char[strlen(buf) + 1];
        strcpy(current_line->line,buf);
        //printf("svgRecursivePrintf(%s,%s,%s) new_line=%s\n",objectname,tag,text,current_line->line);
      }
      else // search and replace
      {
        sprintf(buf,"%s\"",tag);
        cptr = strchr(current_line->line,'=');
        if(cptr == NULL) return -1;
        cptr++; cptr++;
        cptr2 = strrchr(cptr,'\"');
        if(cptr2 == NULL) return -1;
        *cptr2 = '\0';
        QString qbuf(cptr);
        QRegExp rx(text,Qt::CaseSensitive,QRegExp::Wildcard);
        qbuf.replace(rx,after);
        strcat(buf,qbuf.toUtf8());
        strcat(buf,"\"\n");
        //printf("svgPrintf(%s,%s,%s) old_line=%s\n",objectname,tag,text,current_line->line);
        delete [] current_line->line;
        current_line->line = new char[strlen(buf) + 1];
        strcpy(current_line->line,buf);
        //printf("svgPrintf(%s,%s,%s) new_line=%s\n",objectname,tag,text,current_line->line);
      }
    }
    current_line = current_line->next;
    if(num_open <= 0) break;
  }
  return len;
}

int pvSvgAnimator::svgTextPrintf(const char *objectname, const char *text)
{
  char buf[MAXARRAY+40];
  SVG_LINE *current_line = first;
  SVG_LINE *last_open = NULL;
  SVG_LINE *last = NULL;
  int i,ilast;
  int len = strlen(objectname);
  if(first == NULL) return -1;
  ilast = 0;

  if(opt.arg_debug) printf("svgTextPrintf(%s,%s)\n",objectname,text);
  for(i=0; i<num_lines; i++) // find objectname
  {
    if(current_line->line[0] == '<') 
    {
      ilast = i;
      last_open = current_line;
    }
    if(strncmp(current_line->line,"id=",3) == 0)
    {
      //fix if(strstr(current_line->line,objectname) != NULL)
      if(mystrncmp(&current_line->line[4],objectname) == 0)
      {
        break;
      }
    }
    current_line = current_line->next;
  }
  if(i >= num_lines) return -1;
  for(i=ilast; i<num_lines; i++) // '>'
  {
    if(strncmp(current_line->line,">",1) == 0)
    {
      break;
    }
    if(strncmp(current_line->line,"/>",2) == 0)
    {
      break;
    }
    last = current_line;
    current_line = current_line->next;
  }
  if(i >= num_lines) return -1;

  last = current_line; // the line with '>'
  current_line = current_line->next;

  //perhaps <tspan
  if(strstr(current_line->line,"<tspan") != NULL)
  {
    while(current_line != NULL) // '>'
    {
      if(strncmp(current_line->line,">",1) == 0)
      {
        break;
      }
      last = current_line;
      current_line = current_line->next;
    }
    if(current_line != NULL) current_line = current_line->next;
    buf[0] = '\0';
    len = strlen(text);
    if(len < (int) sizeof(buf)-1) strcpy(buf,text); //sprintf(buf,"%s\n",text); //xlehrig
    delete [] current_line->line;
    current_line->line = new char[strlen(buf) + 1];
    strcpy(current_line->line,buf);
    return len;
  }

  buf[0] = '\0';
  len = strlen(text);
  if(len < (int) sizeof(buf)-1) strcpy(buf,text); //sprintf(buf,"%s\n",text); //xlehrig
  delete [] current_line->line;
  current_line->line = new char[strlen(buf) + 1];
  strcpy(current_line->line,buf);
  return len;
}

int pvSvgAnimator::show(const char *objectname, int state)
{
  int i,ilast,len,open_cnt;
  SVG_LINE *current_line = first;
  SVG_LINE *last_open = NULL;
  len = strlen(objectname);
  if(first == NULL) return -1;

  if(opt.arg_debug) printf("svgShow(%s,%d)\n",objectname,state);
  ilast = 0;
  for(i=0; i<num_lines; i++) // find objectname
  {
    if(current_line->line[0] == '<')
    {
      ilast = i;
      last_open = current_line;
    }
    if(strncmp(current_line->line,"id=",3) == 0)
    {
      //fix if(strstr(current_line->line,objectname) != NULL)
      if(mystrncmp(&current_line->line[4],objectname) == 0)
      {
        break;
      }
      else if(strncmp(&current_line->line[4],"pvb:",4) == 0 && 
              mystrncmp(&current_line->line[8],objectname) == 0)
      {
        break;
      }
    }
    current_line = current_line->next;
  }
  if(i >= num_lines) return -1;

  if(last_open != NULL) 
  {
    open_cnt = 0;
    current_line = last_open;
    for(i=ilast; i<num_lines; i++) // set comment
    {
      if(opt.arg_debug) printf("open_cnt=%d %s\n", open_cnt, current_line->line);
      if(i > ilast && open_cnt == 0) return 0;
      if     (strncmp(current_line->line,"</",2) == 0) open_cnt--;
      else if(strncmp(current_line->line,"<",1)  == 0) open_cnt++;
      else if(strncmp(current_line->line,"/>",2) == 0) open_cnt--;
      if(state == 0 && open_cnt >= 0)
      {
        comment[i] = '#';
      }
      else if(open_cnt >= 0)
      {
        comment[i] = ' ';
      }
      if(open_cnt < 0 || (open_cnt == 0 && strncmp(current_line->line,"/>",2) == 0)) 
      { 
        return 0;
      }
      current_line = current_line->next;
    }
  }
  return -1;
}

int pvSvgAnimator::testoutput()
{
  int i;
  printf("pvSvgAnimator::testoutput start\n");
  SVG_LINE *current_line = first;
  if(first == NULL)
  {
    printf("pvSvgAnimator::testoutput first == NULL\n");
    return -1;
  }
  for(i=0; i<num_lines; i++)
  {
    printf("pvSvgAnimator:line=%s\n",current_line->line);
    current_line = current_line->next;
  }
  printf("pvSvgAnimator::testoutput return\n");
  return 0;
}

#if QT_VERSION >= 0x040201
int pvSvgAnimator::perhapsSetOverrideCursor(int xmouse, int ymouse, float zoomx, float zoomy, int buttons)
{
  QString  buf, name;
  QRectF   bounds, mappedBounds;
  QMatrix  matrix;
  int      iline, x,y;
  SVG_LINE *svgline;

  if(zoomx < 0.001f || zoomy < 0.001f) return -1;
  x = (int) (xmouse / zoomx);
  y = (int) (ymouse / zoomy);

  iline = 0;
  svgline = first;
  while(svgline != NULL)
  {
    if((strncmp(svgline->line,"id=\"PV.",7) == 0 || 
        strncmp(svgline->line,"id=\"@@",6)  == 0 ) && comment[iline] == ' ') // visual feedback ?
    {
      name = &svgline->line[4];
      name.remove('\"');
      bounds = draw->renderer.boundsOnElement(name);
      matrix = draw->renderer.matrixForElement(name);

      mappedBounds =  matrix.mapRect(bounds); 
      if(x >= mappedBounds.x() && x <= (mappedBounds.x()+mappedBounds.width()) &&
         y >= mappedBounds.y() && y <= (mappedBounds.y()+mappedBounds.height()) )
      {
        if(opt.arg_debug) printf("perhapsSetOverrideCursor:: inside\n");
        QApplication::setOverrideCursor(QCursor(Qt::PointingHandCursor));
        return 0;
      }
    }
    svgline = svgline->next;
    iline++;
  }

  if(draw != NULL && buttons == Qt::NoButton) QApplication::setOverrideCursor(QCursor((Qt::CursorShape) draw->originalCursor));
  return 0;
}

int pvSvgAnimator::perhapsSendSvgEvent(const char *event, int *s, int id, int xmouse, int ymouse, float zoomx, float zoomy)
{
  QString  buf, name;
  QRectF   bounds, mappedBounds;
  QMatrix  matrix;
  int      iline, x,y;
  SVG_LINE *svgline;

  if(zoomx < 0.001f || zoomy < 0.001f) return -1;
  x = (int) (xmouse / zoomx);
  y = (int) (ymouse / zoomy);

  iline = 0;
  svgline = first;
  while(svgline != NULL)
  {
    if((strncmp(svgline->line,"id=\"pv.",7) == 0 || 
        strncmp(svgline->line,"id=\"PV.",7) == 0 || 
        strncmp(svgline->line,"id=\"@",5)   == 0 ) && comment[iline] == ' ') // send click event ?
    {
      name = &svgline->line[4];
      name.remove('\"');
      bounds = draw->renderer.boundsOnElement(name);
      if(opt.arg_debug) printf("id=%s bounds(%f,%f,%f,%f)\n", (const char *) name.toUtf8(), bounds.x(), bounds.y(), bounds.width(), bounds.height());

      matrix = draw->renderer.matrixForElement(name);

      mappedBounds =  matrix.mapRect(bounds); 
      if(opt.arg_debug) printf("id=%s mappedBounds(%f,%f,%f,%f)\n", (const char *) name.toUtf8(), mappedBounds.x(), mappedBounds.y(), mappedBounds.width(), mappedBounds.height());

      if(x >= mappedBounds.x() && x <= (mappedBounds.x()+mappedBounds.width()) &&
         y >= mappedBounds.y() && y <= (mappedBounds.y()+mappedBounds.height()) )
      {
        if(opt.arg_debug) printf("inside\n");
        buf.sprintf("text(%d,\"%s=%s\")\n",id,event,(const char *) name.toUtf8());
        if(buf.length() < MAX_PRINTF_LENGTH)
        {
          tcp_send(s, buf.toUtf8(), strlen(buf.toUtf8())); // send TEXT_EVENT to pvserver
        }
      }
    }
    svgline = svgline->next;
    iline++;
  }

  return 0;
}
#endif
