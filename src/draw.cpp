/**
 * @file draw.cpp
 * @author Jin (jinj2@illinois.edu)
 * @brief 
 * @version 0.1
 * @date 2024-11-07
 * 
 * @copyright Copyright (c) 2024
 * 
 */
#include "../include/all.hpp"

#include <cmath>
#include <algorithm>
#include <climits>
#include <random>
#include <omp.h>



#define EPSILON 0.001
/**
 * @brief The draw function that loops over the screen of pixels.
 * @param img The image to draw on.
 * 
 */
void draw(Image& img){
	//loop over all pixels	
	// Update bar state
	//int totalPixels = width * height;
    //ProgressBar progressBar(totalPixels);

	#pragma omp parallel for collapse(2) schedule(dynamic)
	for(int y = 0; y < height;y++){
		for(int x = 0; x < width;x++){
			RGBA rgba;
			if(aa == 0){
				rgba = shootPrimaryRay((double)x,(double)y);
				setImageColor(img,rgba,x,y);
			}else{
				RGBA new_rgba;
				for(int _ = 0;_ < aa;_++){
					double new_x = x + randD(-0.5,0.5);
					double new_y = y + randD(-0.5,0.5);
					new_rgba = new_rgba + shootPrimaryRay(new_x,new_y);
					setImageColor(img,new_rgba.mean(aa),x,y);
				}
			}

			//progressBar.update(y * width + x + 1);

		}
	}
	//progressBar.finish();
}

/**
 * @brief Shoots a primary ray into the scene, at pixel location (x,y).
 * @param x x coordinate of the pixel.
 * @param y y coordinate of the pixel.
 * @return RGBA the final pixel color value, in linear RGB color space.
 * @details This function shoots one primary ray for the pixel (x,y). Multiple
 *          calls achieve anti-aliasing.
 */
RGBA shootPrimaryRay(double x,double y){
	//create a ray
	Ray ray(x,y);
	
	//loop over all objects in the scene
	ObjectInfo obj = hitNearest(ray);

	RGBA color,diffuse,reflect,refract,gi_color;
	if(obj.isHit){ //hit
		diffuse = diffuseLight(obj);
		reflect = reflectionLight(ray,obj);
		refract = refractionLight(ray,obj);
		gi_color = obj.color * globalIllumination(obj,gi);
		//mix the colors
		color = obj.shine * reflect + 
				(RGB(1,1,1) - obj.shine) * obj.trans * refract + 
				(RGB(1,1,1) - obj.shine) * (RGB(1,1,1) - obj.trans) * (diffuse + gi_color);
		color.a = 1.0;
	}else hitMiss(); //hitMiss() does nothing

	return color;
}

/**
 * @brief Gets the nearest object in the path of the ray.
 * @param ray The ray to trace.
 * @return ObjectInfo All the related info for the object, if any is hit.
 */
ObjectInfo hitNearest(Ray& ray){
	if(ray.bounce == 0) return ObjectInfo();
	auto object_tuple = bvh_head->checkObject(ray);
	auto plane_tuple = checkPlane(ray);
	auto closest_object = unpackIntersection(object_tuple,plane_tuple);
	return closest_object;
}

/**
 * @brief Does nothing.
 * @return ObjectInfo 
 */
ObjectInfo hitMiss(){
	return ObjectInfo();
}

RGBA diffuseLight(const ObjectInfo& obj){
	RGBA color;
	vec3 normal = obj.normal;
	if(obj.rough > 0)
		normal = normal + vec3(standerdD(obj.rough),standerdD(obj.rough),standerdD(obj.rough));
	normal = normal.normalize();
	for(auto& light : sun){
		//Create a shadow ray, check if path blocked
		Ray shadow_ray(obj.i_point + obj.normal*EPSILON,light.dir,1);
		auto sunInfo = hitNearest(shadow_ray);
		if(sunInfo.isHit) continue;
		double lambert = std::max(dot(normal,light.dir.normalize()),0.0);
		color = color + getColorSun(lambert,obj.color,light.color);
		}
		//iterate over all point lights(bulbs)
	for(auto& light : bulbs){
		//Create a shadow ray, check if path blocked
		vec3 bulbDir = (light.point - obj.i_point);
		Ray shadow_ray(obj.i_point + obj.normal*EPSILON,bulbDir,1);
		auto bulbInfo = hitNearest(shadow_ray);
		if(bulbInfo.isHit){
			if(bulbInfo.distance < bulbDir.length()) continue;
		}
		double lambert = std::max(dot(normal,bulbDir.normalize()),0.0);
		color = color + getColorBulb(lambert,obj.color,light.color,bulbDir.length());
	}
	return color;
}

RGBA reflectionLight(const Ray& ray,const ObjectInfo& obj){
	if(obj.shine == RGB(0.0,0.0,0.0) || ray.bounce <= 0) return RGBA();

	vec3 normal = obj.normal;
	if(obj.rough > 0)
		normal = normal+vec3(standerdD(obj.rough),standerdD(obj.rough),standerdD(obj.rough));
	normal = normal.normalize();
	vec3 reflect_dir = ray.dir - 2*(dot(normal,ray.dir))*normal;
	Ray second_ray(obj.i_point + obj.normal*EPSILON,reflect_dir,ray.bounce - 1);

	ObjectInfo second_obj = hitNearest(second_ray);

	RGB shine,trans;
	if(ray.bounce == 1){
		shine = RGB(0.0,0.0,0.0);
		trans = RGB(0.0,0.0,0.0);
	}else{
		shine = second_obj.shine;
		trans = second_obj.trans;
	}

	RGBA color,diffuse,reflect,refract;
	if(second_obj.isHit){ //hit
		diffuse = diffuseLight(second_obj);
		reflect = reflectionLight(second_ray,second_obj);
		refract = refractionLight(ray,obj);
		//refract = RGBA(0,0,0,1);
		//mix the colors
		color = shine * reflect + 
				(RGB(1,1,1) - shine) * trans * refract + 
				(RGB(1,1,1) - shine) * (RGB(1,1,1) - trans) * diffuse;
	}else color = RGBA(0,0,0,1);

	return color;
}

RGBA refractionLight(const Ray& ray,const ObjectInfo& obj){
	if(obj.trans == RGB(0.0,0.0,0.0) || ray.bounce <= 0) return RGBA();
	vec3 refract_dir;
	Ray inside_ray,final_ray;
	double ior = 1 / obj.ior;
	vec3 dir = ray.dir;
	vec3 normal = obj.normal.normalize();
	point3 i_point = obj.i_point;
	int bounce = ray.bounce;
	double k = 1.0 - std::pow(ior,2) * (1.0 - std::pow(dot(normal,dir),2));
	if(k < 0){ //total internal refraction
		//use the reflection method instead
		refract_dir = dir - 2*(dot(normal,dir))*normal;
		final_ray = Ray(i_point + normal*EPSILON,refract_dir,--bounce);
	}else{ //refraction inside the object
		refract_dir = ior * dir - (ior * (dot(normal,dir)) + std::sqrt(k)) * normal;
		inside_ray = Ray(i_point - normal*1e-4,refract_dir,bounce);
		//The object that the light goes out,usually the same sphere
		ObjectInfo other_obj = hitNearest(inside_ray);
		normal = other_obj.normal.normalize();
		ior = other_obj.ior;
		dir = inside_ray.dir;
		i_point = other_obj.i_point;
		k = 1.0 - ior * ior * (1.0 - std::pow(dot(normal,dir),2));
		refract_dir = ior * dir - (ior * (dot(normal,dir)) + std::sqrt(k)) * normal;
		final_ray = Ray(i_point - normal*1e-4,refract_dir,--bounce);
	}
	ObjectInfo final_obj = hitNearest(final_ray);
	RGB shine,trans;
	if(bounce == 0){
		shine = RGB(0.0,0.0,0.0);
		trans = RGB(0.0,0.0,0.0);
	}else{
		shine = final_obj.shine;
		trans = final_obj.trans;
	}

	RGBA color,diffuse,reflect,refract;
	if(final_obj.isHit){ //hit
		diffuse = diffuseLight(final_obj);
		reflect = reflectionLight(final_ray,final_obj);
		refract = refractionLight(final_ray,final_obj);
		//mix the colors
		color = shine * reflect + 
				(RGB(1,1,1) - shine) * trans * refract + 
				(RGB(1,1,1) - shine) * (RGB(1,1,1) - trans) * diffuse;
	}else color = RGBA(0,0,0,1);

	return color;
}

RGBA globalIllumination(const ObjectInfo& obj,int gi_bounce){
	if(gi == 0 || gi_bounce == 0) return RGBA(); //exit if global illumination disabled
	vec3 normal = obj.normal;
	point3 i_point = obj.i_point;
	//sample a point on the unit sphere, with the center being the intersection point
	vec3 gi_dir = (normal + spherePoint()).normalize();
	Ray gi_ray(i_point + normal * EPSILON,gi_dir,gi_bounce-1);
	ObjectInfo gi_obj = hitNearest(gi_ray);
	RGBA color,diffuse,reflect,refract,gi_color;
	if(gi_obj.isHit){ //hit
		diffuse = diffuseLight(gi_obj);
		reflect = reflectionLight(gi_ray,gi_obj);
		refract = refractionLight(gi_ray,gi_obj);
		gi_color = gi_obj.color * globalIllumination(gi_obj,gi_bounce - 1);
		//mix the colors
		color = gi_obj.shine * reflect + 
				(RGB(1,1,1) - gi_obj.shine) * gi_obj.trans * refract + 
				(RGB(1,1,1) - gi_obj.shine) * (RGB(1,1,1) - gi_obj.trans)*(diffuse + gi_color);
		color.a = 1.0;
	}else hitMiss(); //hitMiss() does nothing
	return color;
}


/**
 * @brief Check if any planes are intersecting with the ray.
 * @param ray The ray to check against.
 * @param exit_early For shadow checking purposes, exit early if a plane is in the
 *        way, casting shadows. Do not set to true with bulb(light in scene).
 * @return std::tuple<double,point3,vec3,RGB> The distance to the plane(<0 no intersection), the 
 *          intersecting point,the normal, the color of the plane.
 */
ObjectInfo checkPlane(Ray& ray, bool exit_early){
	double t_sol = INT_MAX;
	point3 p_sol;
	vec3 nor;
	RGB p_color;
	RGB shine;
	RGB trans;
	double ior = 1.458;
	double rough = 0;
	for(auto& plane:planes){
		double t = dot((plane.point - ray.eye),plane.nor) / (dot(ray.dir,plane.nor));
		if(t <= 0) continue;
		point3 intersection_point = t * ray.dir + ray.eye;
		if(t < t_sol && t > EPSILON){
			t_sol = t;
			p_sol = intersection_point;
			p_color = plane.color;
			nor = (dot(plane.nor,ray.dir) < 0) ? plane.nor : -plane.nor;
			shine = plane.shininess;
			trans = plane.trans;
			ior = plane.ior;
			rough = plane.roughness;
		}
	}
	if(t_sol >= INT_MAX - 10) return ObjectInfo(); 
	return ObjectInfo(t_sol,p_sol,nor,p_color,shine,trans,ior,rough); 
}




/**
 * @brief Get the Color of Sun (directional light)
 * @param lambert The lambert constant.
 * @param objColor The color of the object.
 * @param lightColor The color of the light.
 * @return RGB The linear RGB color, after blending.
 * @details Takes care of color exposure.
 */
RGBA getColorSun(double lambert,RGB objColor,RGB lightColor){
	double r,g,b;
	r = objColor.r * (lightColor.r * lambert);
	g = objColor.g * (lightColor.g * lambert);
	b = objColor.b * (lightColor.b * lambert);
	return RGBA(r,g,b,0.0);
}

/**
 * @brief Get the Color of Bulb (scene light)
 * @param lambert The lambert constant.
 * @param objColor The color of the object.
 * @param lightColor The color of the light.
 * @return RGB The linear RGB color, after blending.
 * @details Takes care of color exposure. Applys light intensity falloff.
 */
RGBA getColorBulb(double lambert,RGB objColor,RGB lightColor,double t){
	double r,g,b;
	double i = 1.0f / std::pow(t,2);
	r = objColor.r * (lightColor.r * lambert);
	g = objColor.g * (lightColor.g * lambert);
	b = objColor.b * (lightColor.b * lambert);
	return RGBA(setExpose(r)*i,setExpose(g)*i,setExpose(b)*i,0.0);
}

// /**
//  * @brief Check if a sphere intersects with a given ray.
//  * @param ray The ray to be checked.
//  * @param exit_early For shadow checking purposes, exit early if a sphere is in the
//  *        way, casting shadows. Do not set to true with bulb(light in scene).
//  * @return std::tuple<double,point3,vec3,RGB> The distance to the sphere(<0 no intersection), the 
//  *          intersecting point,the normal, the color of the sphere.
//  * @deprecated 
//  */
// ObjectInfo checkSphere(Ray& ray, bool exit_early){
// 	double t_sol = INT_MAX;
// 	point3 p_sol;
// 	vec3 nor;
// 	RGB s_color;
// 	for(auto& s_ptr : spheres){
// 		std::shared_ptr<Sphere> sphere = std::dynamic_pointer_cast<Sphere>(s_ptr);
// 		vec3 cr0 = (sphere->c - ray.eye);
// 		bool inside = (pow(cr0.length(),2) < pow(sphere->r,2));
// 		double tc = cr0.dot(ray.dir) / ray.dir.length();

// 		if(!inside && tc < 0) continue;

// 		vec3 d = ray.eye + (tc * ray.dir) - sphere->c;
// 		double d2 = pow(d.length(),2);

// 		if(!inside && pow(sphere->r,2) < d2) continue;

// 		//difference between t and tc
// 		//the two intersecting points are generated
// 		double t_offset = std::sqrt(pow(sphere->r,2) - d2) / ray.dir.length();
// 		double t;
// 		if(inside) t = tc + t_offset;
// 		else t = tc - t_offset;
// 		point3 p = t * ray.dir + ray.eye;
// 		//update when the current sphere is the closest
// 		if(t < t_sol && t > EPSILON){
// 			if(exit_early) return ObjectInfo(100,point3(),vec3(),RGB());
// 			t_sol = t;
// 			p_sol = p;
// 			nor = 1/sphere->r * (p_sol - sphere->c);
// 			s_color = sphere->getColor(p_sol);
// 		}
// 	}
// 	if(t_sol >= INT_MAX - 10) return ObjectInfo();
// 	return ObjectInfo(t_sol,p_sol,nor,s_color); 
// }

// /**
//  * @brief Check if any triangles are intersecting with the ray.
//  * @param ray The ray to check against.
//  * @param exit_early For shadow checking purposes, exit early if a triangles is in the
//  *        way, casting shadows. Do not set to true with bulb(light in scene).
//  * @return std::tuple<double,point3,vec3,RGB> The distance to the triangle(<0 no intersection), the 
//  *          intersecting point,the normal, the color of the triangle.
//  * @deprecated
//  */
// ObjectInfo checkTriangle(Ray& ray, bool exit_early){
// 	double t_sol = INT_MAX;
// 	point3 p_sol;
// 	vec3 nor;
// 	RGB t_color;
// 	for(auto& tri:triangles){
// 		double t = dot((tri.p0 - ray.eye),tri.nor) / (dot(ray.dir,tri.nor));
// 		if(t <= 0) continue;
// 		point3 intersection_point = t * ray.dir + ray.eye;
// 		auto [b0,b1,b2] = getBarycentric(tri,intersection_point);
// 		bool inside = (std::min({b0, b1, b2}) >= -EPSILON);
// 		if(t < t_sol && t > EPSILON && inside){
// 			if(exit_early) return ObjectInfo(100,point3(),vec3(),RGB());;
// 			t_sol = t;
// 			p_sol = intersection_point;
// 			t_color = tri.getColor(b0,b1,b2);
// 			nor = (dot(tri.nor,ray.dir) < 0) ? tri.nor : -tri.nor;
			
// 		}
// 	}
// 	if(t_sol >= INT_MAX - 10) return ObjectInfo(); 
// 	return ObjectInfo(t_sol,p_sol,nor,t_color); 
// }