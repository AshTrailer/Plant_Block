close all; clear; clc;

model = createpde('thermal','transient');

Tmin = 20; T1 = 30; T2 = 40; Tmax = 50;
cmap_colors = [
    0 0 1;    % 蓝
    0 1 0;    % 绿
    1 1 0;    % 黄
    1 0 0     % 红
];

L = 0.3;   % X方向 30 cm
W = 0.04;  % Y方向 4 cm
H = 0.3;   % Z方向 30 cm
gm = multicuboid(L,W,H); 
model.Geometry = gm;

figure
pdegplot(model,'FaceLabels','on','FaceAlpha',0.5)
title('PU立方块')
xlabel('X (m)'); ylabel('Y (m)'); zlabel('Z (m)')
axis equal; grid on

thermalProperties(model,'ThermalConductivity',0.2,... 
                          'MassDensity',1200,...
                          'SpecificHeat',1500);

thermalIC(model,25);

Lq = 0.04;   % X方向长度
Wq = 0.01;   % Z方向高度
Hq = 0.04;   % Z方向高度
y_heat = W;  % 顶面
x0 = L/2;    % X中心
z0 = H/2;    % Z中心
q0 = 1e6;    % 热源强度 W/m^3

internalHeatFunc = @(location,state) ...
    q0 * (abs(location.x - x0) <= Lq/2 & ...
          abs(location.y - y_heat) <= Wq/2 & ...   % 薄层贴顶面
          abs(location.z - z0) <= Hq/2);
internalHeatSource(model, internalHeatFunc);

h = 10; Tinf = 25;
thermalBC(model,'Face',[1,2,3,4,5,6],...
          'ConvectionCoefficient',h,...
          'AmbientTemperature',Tinf);

generateMesh(model,'Hmax',0.01);  % 足够精细以捕捉小热源

tlist = 0:2:50;

result = solve(model,tlist);

T_points = [Tmin T1 T2 Tmax];
nColors = 256;
T_interp = linspace(Tmin,Tmax,nColors);
r = interp1(T_points,cmap_colors(:,1),T_interp);
g = interp1(T_points,cmap_colors(:,2),T_interp);
b = interp1(T_points,cmap_colors(:,3),T_interp);
customCMap = [r' g' b'];

figure
for k = 1:length(tlist)
    pdeplot3D(model,'ColorMapData',result.Temperature(:,k))
    title(['PU立方块温度分布 t = ', num2str(tlist(k)),' s'])
    xlabel('X (m)'); ylabel('Y (m)'); zlabel('Z (m)')
    colormap(customCMap)
    colorbar
    axis equal
    caxis([Tmin Tmax])
    drawnow
end
