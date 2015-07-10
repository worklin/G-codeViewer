
#include "modeldata.h"
#include "modelloader.h"
#include "../Interface/loadingbar.h"

#include <QtOpenGL>
#include <QFileInfo>


//////////////////////////////////////
//Public 
//////////////////////////////////////
ModelData::ModelData(GreenTech* main, bool bypassDisplayLists)
{
	pMain = main;
	filepath = "";
    loadedcount = 0;
    displaySkipping = 0;
    bypassDispLists = bypassDisplayLists;

	maxbound = QVector3D(-999999.0,-999999.0,-999999.0);
	minbound = QVector3D(999999.0,999999.0,999999.0);

}

ModelData::~ModelData()
{
    unsigned int i;
    triList.clear();
	for(i=0; i < instList.size();i++)
	{
		delete instList[i];
	}

    for( i = 0; i < normDispLists.size(); i++)
    {
        glDeleteLists(normDispLists[i],1);
    }
    for( i = 0; i < flippedDispLists.size(); i++)
    {
        glDeleteLists(flippedDispLists[i],1);
    }
}
QString ModelData::GetFilePath()
{	
	return filepath;
}
QString ModelData::GetFileName()
{
	return filename;
}
//data loading
bool ModelData::LoadIn(QString filepath)
{	

    {
        bool loaderReady;
        bool abort;

        STLTri* pLoadedTri = NULL;
        Triangle3D newtri;

        this->filepath = filepath;

        if(filepath.isEmpty())
            return false;

        //extract filename from path!
        filename = QFileInfo(filepath).fileName();

        ModelLoader mLoader(filepath,loaderReady,NULL);

        if(loaderReady == false)//error opening model data
        {
            //display Loader Error
            QMessageBox msgBox;
            msgBox.setText(mLoader.GetError());
            msgBox.exec();
            return false;
        }

        //make a progress bar and connect it to
        LoadingBar loadbar(0,100);
        loadbar.useCancelButton(false);
        loadbar.setDescription("Analysising the model" + filename +"...");
        QObject::connect(&mLoader,SIGNAL(PercentCompletedUpdate(qint64,qint64)),
                         &loadbar,SLOT(setProgress(qint64,qint64)));
        //now we are ready to walk the loader through reading each triangle
        //and copying it into the this model data.
        while(mLoader.LoadNextTri(pLoadedTri,abort))
        {
            if(abort)
            {
                //display Loader abort error
                QMessageBox msgBox;
                msgBox.setText(mLoader.GetError());
                msgBox.exec();
                return false;
            }
            else
            {
                //newtri.normal.setX(pLoadedTri->nx);
                //newtri.normal.setY(pLoadedTri->ny);
                //newtri.normal.setZ(pLoadedTri->nz);

                newtri.vertex[0].setX(pLoadedTri->x0);
                newtri.vertex[0].setY(pLoadedTri->y0);
                newtri.vertex[0].setZ(pLoadedTri->z0);
                newtri.vertex[1].setX(pLoadedTri->x1);
                newtri.vertex[1].setY(pLoadedTri->y1);
                newtri.vertex[1].setZ(pLoadedTri->z1);
                newtri.vertex[2].setX(pLoadedTri->x2);
                newtri.vertex[2].setY(pLoadedTri->y2);
                newtri.vertex[2].setZ(pLoadedTri->z2);

                //use right hand rule for importing normals - ignore file normals..
                newtri.UpdateNormalFromGeom();          //获取一个面的单位法向量
                delete pLoadedTri;
                newtri.UpdateBounds();                 //更新一遍边界信息
                triList.push_back(newtri);              //将一个个三角面的信息放入到这个triList列表中去
            }
        }
        qDebug() << "Loaded triangles: " << triList.size();
        //now center it!
        CenterModel();

        //generate a normal display lists.

        int displaySuccess = FormNormalDisplayLists();

        if(displaySuccess)
            return true;
        else
            return false;
    }

}
//instance
ModelInstance* ModelData::AddInstance()
{
	loadedcount++;
    ModelInstance* pNewInst = new ModelInstance(this);
	instList.push_back(pNewInst);
	pNewInst->SetTag(filename + " " + QString().number(loadedcount));
	return pNewInst;
}


//////////////////////////////////////
//Private
//////////////////////////////////////
void ModelData::CenterModel()
{
	//figure out what to current center of the models counds is..
    //计算当前模型的中心位置
    unsigned int f;
    unsigned int v;
	QVector3D center;
    for(f=0;f<triList.size();f++)           //遍历一次三角面的列表
	{
		for(v=0;v<3;v++)
		{
			//update the models bounds.
			//max
            if(triList[f].maxBound.x() > maxbound.x())
			{
                maxbound.setX(triList[f].vertex[v].x());
			}
            if(triList[f].maxBound.y() > maxbound.y())
			{
                maxbound.setY(triList[f].vertex[v].y());
			}
            if(triList[f].maxBound.z() > maxbound.z())
			{
                maxbound.setZ(triList[f].vertex[v].z());
			}
			
			//mins
            if(triList[f].minBound.x() < minbound.x())
			{
                minbound.setX(triList[f].vertex[v].x());
			}
            if(triList[f].minBound.y() < minbound.y())
            {
                minbound.setY(triList[f].vertex[v].y());
			}
            if(triList[f].minBound.z() < minbound.z())
			{
                minbound.setZ(triList[f].vertex[v].z());
			}
		}
	}

    center = (maxbound + minbound)*0.5;         //求得中心位置

    for(f=0;f<triList.size();f++)
	{
		for(v=0;v<3;v++)
		{
            triList[f].vertex[v] -= center;
		}
        triList[f].UpdateBounds(); // since we are moving every triangle, we need to update their bounds too.
	}
	maxbound -= center;
	minbound -= center;
}

//rendering
//generate opengl display list, flipx generates with inverted x normals and vertices
bool ModelData::FormNormalDisplayLists() //returns opengl error enunum.
{
    unsigned int l;
    unsigned int t;
    const unsigned int listSize = 10000;//each list to be 10000 triangles big.
    unsigned int tSeamCount = 0;

    //first lets free up existing display lists if there are any
    //释放存在的显示列表
    for( l = 0; l < normDispLists.size(); l++)
    {
        glDeleteLists(normDispLists[l],1);
    }

    if(bypassDispLists)
        return true;

    normDispLists.push_back(glGenLists(1));             //使用glGenLists产生一个法向量列表
    if(normDispLists.at(normDispLists.size()-1) == 0)
        return false;//failure to allocate a list index???

    glNewList(normDispLists.at(normDispLists.size()-1),GL_COMPILE);
    glBegin(GL_TRIANGLES);// Drawing Using Triangles
    for(t = 0; t < triList.size(); t = t + 1 + displaySkipping)//for each triangle
    {
        glNormal3f(triList[t].normal.x(),triList[t].normal.y(),triList[t].normal.z());//normals，向量

        glVertex3f( triList[t].vertex[0].x(), triList[t].vertex[0].y(), triList[t].vertex[0].z());
        glVertex3f( triList[t].vertex[1].x(), triList[t].vertex[1].y(), triList[t].vertex[1].z());
        glVertex3f( triList[t].vertex[2].x(), triList[t].vertex[2].y(), triList[t].vertex[2].z());

        //make a new seam.
        if(tSeamCount >= listSize)          //如果大于了10000个三角面，先结束gl的渲染，然后添加三角列表，再重新开始渲染
        {
            glEnd();        //结束
            glEndList();
            normDispLists.push_back(glGenLists(1));
            if(normDispLists.at(normDispLists.size()-1) == 0)
                return false;//failure to allocate a list index???

            //when creating a seam check for graphics error
            int err = GetGLError();
            if(err)
            {
                if(err == GL_OUT_OF_MEMORY)
                {
                    displaySkipping += 10;
                    return FormNormalDisplayLists();
                }
                else
                    return false;
            }
            glNewList(normDispLists.at(normDispLists.size()-1),GL_COMPILE);
            glBegin(GL_TRIANGLES);// Drawing Using Triangles
            tSeamCount = 0;
        }
        tSeamCount++;
    }
    glEnd();
    glEndList();

    int err = GetGLError();
    if(err)
    {
        if(err == GL_OUT_OF_MEMORY)
        {
            displaySkipping += 10;              //如果发生了错误，显示内存将跳过一段
            return FormNormalDisplayLists();
        }
        else
            return false;
    }
    qDebug() << normDispLists.size() << "Normal Display Lists created for model " << filename;
    return true;
}
bool ModelData::FormFlippedDisplayLists() //returns opengl error enunum.
{
    unsigned int l;
    unsigned int t;
    const unsigned int listSize = 10000;//each list to be 10000 triangles big.
    unsigned int tSeamCount = 0;

    //first lets free up existing display lists if there are any
    for( l = 0; l < flippedDispLists.size(); l++)
    {
        glDeleteLists(flippedDispLists[l],1);
    }

    if(bypassDispLists)
        return true;


    flippedDispLists.push_back(glGenLists(1));
    if(flippedDispLists.at(flippedDispLists.size()-1) == 0)
        return false;//failure to allocate a list index???

    glNewList(flippedDispLists.at(flippedDispLists.size()-1),GL_COMPILE);
    glBegin(GL_TRIANGLES);// Drawing Using Triangles
    for(t = 0; t < triList.size(); t = t + 1 + displaySkipping)//for each triangle
    {

        glNormal3f(-triList[t].normal.x(),triList[t].normal.y(),triList[t].normal.z());//normals

        glVertex3f( -triList[t].vertex[2].x(), triList[t].vertex[2].y(), triList[t].vertex[2].z());
        glVertex3f( -triList[t].vertex[1].x(), triList[t].vertex[1].y(), triList[t].vertex[1].z());
        glVertex3f( -triList[t].vertex[0].x(), triList[t].vertex[0].y(), triList[t].vertex[0].z());
        //make a new seam.
        if(tSeamCount >= listSize)
        {
            glEnd();
            glEndList();
            flippedDispLists.push_back(glGenLists(1));
            if(flippedDispLists.at(flippedDispLists.size()-1) == 0)
                return false;//failure to allocate a list index???

            //when creating a seam check for graphics error
            int err = GetGLError();
            if(err)
            {
                if(err == GL_OUT_OF_MEMORY)
                {
                    displaySkipping += 10;
                    return FormFlippedDisplayLists();
                }
                else
                    return false;
            }

            glNewList(flippedDispLists.at(flippedDispLists.size()-1),GL_COMPILE);
            glBegin(GL_TRIANGLES);// Drawing Using Triangles
            tSeamCount = 0;
        }
        tSeamCount++;
    }
    glEnd();
    glEndList();

    int err = GetGLError();
    if(err)
    {
        if(err == GL_OUT_OF_MEMORY)
        {
            displaySkipping += 10;
            return FormFlippedDisplayLists();
        }
        else
            return false;
    }
    qDebug() << flippedDispLists.size() << "Flipped Display Lists created for model " << filename;
    return true;
}

int ModelData::GetGLError()
{
    int displayerror = glGetError();

    if(displayerror)
    {
        //display Assimp Error
        qDebug() << "Display List Error: " << displayerror; //write to log as well.
        QMessageBox msgBox;

        switch(displayerror)
        {
        case GL_OUT_OF_MEMORY:
            msgBox.setText("OpenGL Error:  GL_OUT_OF_MEMORY\nModel is too large to render on your graphics card.\nAttemping To Draw Sparse Triangles.");
            break;
        case GL_INVALID_ENUM:
            msgBox.setText("OpenGL Error:  GL_INVALID_ENUM");
            break;
        case GL_INVALID_VALUE:
            msgBox.setText("OpenGL Error:  GL_INVALID_VALUE");
            break;
        case GL_INVALID_FRAMEBUFFER_OPERATION:
            msgBox.setText("OpenGL Error:  GL_INVALID_FRAMEBUFFER_OPERATION");
            break;
        case GL_STACK_UNDERFLOW:
            msgBox.setText("OpenGL Error:  GL_STACK_UNDERFLOW");
            break;
        case GL_STACK_OVERFLOW:
            msgBox.setText("OpenGL Error:  GL_STACK_OVERFLOW");
            break;
        default:
            break;
        }
        msgBox.exec();
        return displayerror;
   }
   else
        return 0;
}
