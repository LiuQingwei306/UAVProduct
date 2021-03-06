//
// Created by wuwei on 17-7-26.
//

#include "UAVDataList.h"

#include "openMVG/stl/split.hpp"
#include "openMVG/sfm/sfm.hpp"
#include "third_party/cmdLine/cmdLine.h"
#include "third_party/stlplus3/filesystemSimplified/file_system.hpp"
#include "openMVG/exif/exif_IO_EasyExif.hpp"

#include <string>
#include <vector>
#include <stdio.h>
#include <sys/stat.h>
#include <unistd.h>

static  UAVXYZToLatLonWGS84 CooridinateTrans;
using namespace openMVG;
using namespace openMVG::cameras;
using namespace openMVG::sfm;
using namespace openMVG::image;
using namespace openMVG::exif;

static int get_file_size(const char* file) {
    struct stat tbuf;
    stat(file, &tbuf);
    return tbuf.st_size;
}

///////////////////////////////////////////////////////////////////////////////////////////////////////

pair<Vec3,Vec3> UAVPosRead::ReadPOS(fstream *ifs) {
    double x,y,z,r,p,h;
    char line[1024];
    ifs->get(line,1024);
    sscanf(line,"%lf%lf%lf%lf%lf%lf",&x,&y,&z,&r,&p,&h);
    Vec3 trans(x,y,z);
    Vec3 rots(r,p,h);

    return make_pair(trans,rots);
}

//////////////////////////////////////////////////////////////////////////////////////////////////////
//list SFM data list
float UAVDataList::UAVList_CreateSFMList()
{
    //影像文件夹(判断是否存在)
    printf("%s\n",_info_._g_image_dir_.c_str());
    if ( !stlplus::folder_exists( _info_._g_image_dir_ ) )
    {
        std::cerr << "\nThe input directory doesn't exist" << std::endl;
        return EXIT_FAILURE;
    }

    //特征点文件夹
    if ( !stlplus::folder_exists( _info_._g_feature_dir_ ) )
    {
        if ( !stlplus::folder_create(  _info_._g_feature_dir_ ))
        {
            std::cerr << "\nCannot create output directory" << std::endl;
            return EXIT_FAILURE;
        }
    }
    //特征点文件夹
    if ( !stlplus::folder_exists( _info_._g_match_dir_ ) )
    {
        if ( !stlplus::folder_create(  _info_._g_match_dir_ ))
        {
            std::cerr << "\nCannot create output directory" << std::endl;
            return EXIT_FAILURE;
        }
    }

    //几何校正文件夹
    if ( !stlplus::folder_exists( _info_._g_geocorrect_dir_ ) )
    {
        if ( !stlplus::folder_create(  _info_._g_geocorrect_dir_ ))
        {
            std::cerr << "\nCannot create output directory" << std::endl;
            return EXIT_FAILURE;
        }
    }

    //点云数据输出
    if ( !stlplus::folder_exists( _info_._g_point_cloud_dir ) )
    {
        if ( !stlplus::folder_create(  _info_._g_point_cloud_dir ))
        {
            std::cerr << "\nCannot create output directory" << std::endl;
            return EXIT_FAILURE;
        }
    }

    //辅助数据输出
    if ( !stlplus::folder_exists( _info_._g_auxiliary_dir ) )
    {
        if ( !stlplus::folder_create(  _info_._g_auxiliary_dir ))
        {
            std::cerr << "\nCannot create output directory" << std::endl;
            return EXIT_FAILURE;
        }
    }

    //得到文件夹下所有文件
    std::vector<std::string> vec_image = stlplus::folder_files(  _info_._g_image_dir_ );
    std::sort(vec_image.begin(), vec_image.end());

    SfM_Data sfm_data;
    sfm_data.s_root_path =  _info_._g_image_dir_; // Setup main image root_path
    Views & views = sfm_data.views;
    Intrinsics & intrinsics = sfm_data.intrinsics;

    //POS Data
    bool pos_file = false;
    fstream *ifs = NULL;
    UAVPosRead readPOS;
    if(_info_._g_Pos_data!="")
    {
        //POS from file
        pos_file = true;
        ifs->open(_info_._g_Pos_data);
        if(!ifs->is_open())
            pos_file = false;

        for(int i=0;i<_info_._g_Pos_bias;++i){
            pair<Vec3,Vec3> trans_rot =readPOS.ReadPOS(ifs);
        }
    }

    //计算文件大小的参数
    float files_size = 0;

    double width = -1,height = -1,focal = -1;
    C_Progress_display my_progress_bar( vec_image.size(),
                                        std::cout, "\n- Image listing -\n" );
    std::ostringstream error_report_stream;
    std::pair<bool, Vec3> val(true, Vec3::Zero());
    for ( std::vector<std::string>::const_iterator iter_image = vec_image.begin();
          iter_image != vec_image.end(); ++iter_image, ++my_progress_bar )
    {
        _info_._g_ppx=_info_._g_ppy= width = height= focal = -1;
        const std::string sImageFilename = stlplus::create_filespec(  _info_._g_image_dir_, *iter_image );
        const std::string sImFilenamePart = stlplus::filename_part(sImageFilename);
        files_size+=float(get_file_size(sImageFilename.c_str()))/1024.0f;

        ImageHeader imgHeader;
        if (!openMVG::image::ReadImageHeader(sImageFilename.c_str(), &imgHeader))
            continue;

        width = imgHeader.width;
        height = imgHeader.height;
        _info_._g_ppx = width / 2.0;
        _info_._g_ppy = height / 2.0;

        std::unique_ptr<Exif_IO> exifReader(new Exif_IO_EasyExif);
        exifReader->open( sImageFilename );
        if(_info_._g_focal_x == -1||_info_._g_focal_y==-1)
            focal = std::max ( width, height ) * exifReader->getFocal() / _info_._g_ccdsize;
        else
            focal = std::max(_info_._g_focal_x,_info_._g_focal_y);

        _info_._g_focal_x=_info_._g_focal_y=focal;
        std::shared_ptr<IntrinsicBase> intrinsic(NULL);
        if(focal>0&&_info_._g_focal_y>0&&
                _info_._g_ppx>0&&_info_._g_ppy&&height>0&&width>0)
        {
            //initial intrinsic
            intrinsic = std::make_shared<Pinhole_Intrinsic_Radial_K3>
                    (width, height, focal, _info_._g_ppx, _info_._g_ppy, 0.0, 0.0, 0.0);  // setup no distortion as initial guess
        }

        //POS data from Exif
        if(pos_file)
        {
            _info_._g_Has_Pos = true;
            pair<Vec3,Vec3> trans_rot =readPOS.ReadPOS(ifs);
            // Add ECEF XYZ position to the GPS position array
            val.first = true;
            val.second = CooridinateTrans.LatLonToUTM( trans_rot.first(0), trans_rot.first(1),trans_rot.first(2));

            //使用影像内置的POS数据
            ViewPriors v(*iter_image, views.size(), views.size(), views.size(), width, height);

            // Add intrinsic related to the image (if any)
            if (intrinsic == NULL)
            {
                //Since the view have invalid intrinsic data
                // (export the view, with an invalid intrinsic field value)
                v.id_intrinsic = UndefinedIndexT;
            }
            else
            {
                // Add the defined intrinsic to the sfm_container
                intrinsics[v.id_intrinsic] = intrinsic;
            }

            v.b_use_pose_center_ = true;
            v.pose_center_ = val.second;
            //TODO 添加旋转矩阵的初始值如果是POS数据有一个转换的过程
            //v.pose_rotation_

            views[v.id_view] = std::make_shared<ViewPriors>(v);
        } else if ( exifReader->open( sImageFilename ) && exifReader->doesHaveExifInfo() ) {
            // Try to parse EXIF metada & check existence of EXIF data
            _info_._g_Has_Pos = true;
            double latitude, longitude, altitude;
            if ( exifReader->GPSLatitude( &latitude ) &&
                 exifReader->GPSLongitude( &longitude ) &&
                 exifReader->GPSAltitude( &altitude ) )
            {
                // Add ECEF XYZ position to the GPS position array
                val.first = true;
                val.second = CooridinateTrans.LatLonToUTM( latitude, longitude, altitude );

                //使用影像内置的POS数据
                ViewPriors v(*iter_image, views.size(), views.size(), views.size(), width, height);

                // Add intrinsic related to the image (if any)
                if (intrinsic == NULL)
                {
                    //Since the view have invalid intrinsic data
                    // (export the view, with an invalid intrinsic field value)
                    v.id_intrinsic = UndefinedIndexT;
                }
                else
                {
                    // Add the defined intrinsic to the sfm_container
                    intrinsics[v.id_intrinsic] = intrinsic;
                }
                v.b_use_pose_center_ = true;
                v.pose_center_ = val.second;
                views[v.id_view] = std::make_shared<ViewPriors>(v);
            }
        } else{
            _info_._g_Has_Pos = false;
            View v(*iter_image, views.size(), views.size(), views.size(), width, height);
            if (intrinsic == NULL)
            {
                //Since the view have invalid intrinsic data
                // (export the view, with an invalid intrinsic field value)
                v.id_intrinsic = UndefinedIndexT;
            }
            else
            {
                // Add the defined intrinsic to the sfm_container
                intrinsics[v.id_intrinsic] = intrinsic;
            }
            // Add the view to the sfm_container
            views[v.id_view] = std::make_shared<View>(v);
        }
    }
    if (!Save(
            sfm_data,
            _info_._g_SFM_data.c_str(),
            ESfM_Data(VIEWS|INTRINSICS)))
    {
        return EXIT_FAILURE;
    }
    return files_size/1024.0f;
}