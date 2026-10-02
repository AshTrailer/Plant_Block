clear; clc; close all;

%% ================= PARAMETERS =================
% Environment
T_env = 298.15;     % K (25 °C)


% 内腔 (20cm cube)
L_in = 0.20;              % m
V_in = L_in^3;            % m^3
A_in = 6 * L_in^2;        % m^2
rho_air = 1.2;            % kg/m^3
Cp_air = 1005;            % J/(kg*K)
m_air = rho_air * V_in;
C_cold = m_air * Cp_air;  % J/K

% 保温 (PU)
k_PU = 0.025;             % W/(m*K)
d_PU = 0.040;             % m (40 mm)
R_PU = d_PU / (k_PU * A_in);  % K/W
Q_load = @(Tc) (T_env - Tc) / R_PU;   % W, 从环境入到腔内（正数表示进入腔内）

% TEC 参数（估算 / datasheet）
I_max = 6.0;              % A
R_mod = 1.98;             % Ohm
alpha = 0.05;             % V/K (module-level, 可调整)
K_mod = 0.8;              % W/K (module 导热通量系数, 可调整)
tec_size = 0.04;

%{
% 散热（理想水冷）
h_water = 700;           % W/(m^2 K) 强水冷 (可改)
A_hs = 0.04 * 0.04;       % m^2, 40x40 mm 接触面积
hA = h_water * A_hs;      % W/K
%}

h_air = 133;           % W/(m^2*K), 风冷换热系数（经验值）
A_air = 0.05;          % m^2, 散热器有效面积（鳍片+底座）
hA_air = h_air * A_air;  % W/K, 总热端散热能力


% 热容：热端（散热块）的热容（要大于冷端空气的，使数值更稳定）
C_hot = 1005;             % J/K

% 扫描与时间控制
dt = 0.01;                 % s 时间步长
t_end = 1200;              % s 总模拟时间
time = 0:dt:t_end;

% 绘图样式
markerSize = 2;
lineWidth = 1;

%% ======= 定义热力学函数 =======
Qc_fun = @(I, Tc, Th) alpha .* I .* Tc - 0.5 .* I.^2 .* R_mod - K_mod .* (Th - Tc);
Qh_fun = @(I, Tc, Th) alpha .* I .* Tc + 0.5 .* I.^2 .* R_mod + K_mod .* (Th - Tc);
V_fun  = @(I, Tc, Th) I .* R_mod + alpha .* (Th - Tc);
% 热端放热给空气（风冷）
Qh_sink = @(Th) hA_air .* (Th - T_env);  % W
% Qh_sink = @(Th) hA .* (Th - T_env);  % W, 热端传给冷却系统，Th>T_env 时正值

%% =================== 1. 动态仿真 (加入冷端对流散热) ===================
N_points_dyn = 30;
I_list_dyn = linspace(0.01, I_max, N_points_dyn);
colors_dyn = jet(N_points_dyn);
I_dynamic_indices = round(linspace(1, N_points_dyn, min(9, N_points_dyn)));

% 冷端对流参数
h_c = 20;            % W/(m^2*K)，冷端空气对流换热系数
A_c = 0.04*0.04;     % m^2，冷端空气有效散热面积

figure(1); clf; hold on; grid on;
legend_entries = cell(numel(I_dynamic_indices),1);
Tc_hist_all = nan(numel(I_dynamic_indices), numel(time));

for ii = 1:numel(I_dynamic_indices)
    k = I_dynamic_indices(ii);
    I = I_list_dyn(k);
    Tc = T_env;
    Th = T_env;
    Tc_hist = zeros(size(time));
    Th_hist = zeros(size(time));
    Tc_hist(1) = Tc;
    Th_hist(1) = Th;

    for n = 2:length(time)
        % TEC 吸热、腔体热负载
        Qc = Qc_fun(I, Tc, Th);
        Qc_load = Q_load(Tc);

        % 冷端空气散热（对流）
        dTc_dt = (Qc_load - Qc) / C_cold;

        % 热端放热、散热
        Qh = Qh_fun(I, Tc, Th);
        Qh_sink_val = Qh_sink(Th);

        % 温度更新
        dTh_dt = (Qh - Qh_sink_val) / C_hot;

        Tc = Tc + dTc_dt * dt;
        Th = Th + dTh_dt * dt;

        % 数值保护
        Tc = max(Tc, 1); Th = max(Th,1);
        Tc_hist(n) = Tc;
        Th_hist(n) = Th;
    end

    Tc_hist_all(ii, :) = Tc_hist;
    plot(time, Tc_hist - 273.15, 'LineWidth', lineWidth, 'Color', colors_dyn(k,:));
    legend_entries{ii} = sprintf('I=%.2f A', I);
end

xlabel('Time [s]'); ylabel('Cold side Temperature [°C]');
title('Dynamic T_c(t) for selected currents (with cold-side convection)');
legend(legend_entries,'Location','northeast');


%% =================== 2. Qc vs I ===================
N_points_Qc = 200;
I_list_Qc = linspace(0.01, I_max, N_points_Qc);
colors_Qc = jet(N_points_Qc);
Tc_ss = nan(size(I_list_Qc)); Th_ss = nan(size(I_list_Qc));
Qc_ss = nan(size(I_list_Qc)); Pin_ss = nan(size(I_list_Qc));

opts = optimset('Display','off','TolX',1e-8,'MaxIter',300,'MaxFunEvals',500);

for k = 1:numel(I_list_Qc)
    I = I_list_Qc(k);
    fun2 = @(x)[ Qc_fun(I, x(1), x(2)) - Q_load(x(1));
                 Qh_fun(I, x(1), x(2)) - Qh_sink(x(2)) ];
    x0 = [T_env - 5, T_env + 5];
    try
        xsol = fsolve(fun2, x0, opts);
        Tc_ss(k) = xsol(1);
        Th_ss(k) = xsol(2);
        Qc_ss(k) = Qc_fun(I, Tc_ss(k), Th_ss(k));
        Pin_ss(k) = I * V_fun(I, Tc_ss(k), Th_ss(k));
    catch
        Tc_ss(k) = NaN; Th_ss(k) = NaN; Qc_ss(k) = NaN; Pin_ss(k) = NaN;
    end
end

%% =================== 3. COP vs I (基于稳态) ===================
COP_ss = Qc_ss ./ Pin_ss;

% 只保留有效数据：Pin>0 且 Qc>5W
validMask = (Pin_ss > 1e-6) & (Qc_ss > 1);
T_valid = Tc_ss(validMask) - 273.15;  % 转为 °C
COP_valid = COP_ss(validMask);
I_valid = I_list_Qc(validMask);

figure(3); clf;

% 计算导数
dCOP_dI = gradient(COP_valid, I_valid);       % COP 对电流的导数
dT_dCOP = gradient(T_valid, COP_valid);      % 温度对 COP 的导数

% 找 d(T_c)/dCOP 峰值
[~, idxMaxDT] = max(dT_dCOP);
I_peakDT = I_valid(idxMaxDT);
COP_peakDT = COP_valid(idxMaxDT);
T_peakDT = T_valid(idxMaxDT);
dT_peakDT = dT_dCOP(idxMaxDT);

% ========== 上排 ==========

subplot(2,2,1);  % 第一列：COP vs Current
plot(I_valid, COP_valid, 'b-', 'LineWidth', 2); hold on;
scatter(I_valid, COP_valid, 30, 'filled');
% 标出 dT/dCOP 峰值对应点，画辅助虚线
scatter(I_peakDT, COP_peakDT, 80, 'r', 'filled', 'Marker','p');
xline(I_peakDT, 'r--');  % 横坐标
yline(COP_peakDT, 'r--'); % 纵坐标
text(I_peakDT, COP_peakDT, sprintf(' Peak dT/dCOP\n I=%.2fA\n COP=%.2f', I_peakDT, COP_peakDT), ...
    'FontSize',10,'Color','r','VerticalAlignment','bottom','HorizontalAlignment','left');
xlabel('Current I [A]'); ylabel('COP = Q_c / P_{in}');
title('COP vs Current');
grid on;

subplot(2,2,2);  % 第二列：T_c vs COP
plot(COP_valid, T_valid, 'g-', 'LineWidth', 2); hold on;
scatter(COP_valid, T_valid, 30, 'filled');
scatter(COP_peakDT, T_peakDT, 80, 'r', 'filled', 'Marker','p');
xline(COP_peakDT, 'r--');  % 横坐标
yline(T_peakDT, 'r--');    % 纵坐标
text(COP_peakDT, T_peakDT, sprintf(' Peak dT/dCOP\n T=%.2f°C\n COP=%.2f', T_peakDT, COP_peakDT), ...
    'FontSize',10,'Color','r','VerticalAlignment','bottom','HorizontalAlignment','right');
xlabel('COP = Q_c / P_{in}'); ylabel('Cold-side Temperature T_c [°C]');
title('T_c vs COP');
grid on;

% ========== 下排 ==========

subplot(2,2,3);  % 第一列下方：d(COP)/dI
plot(I_valid, dCOP_dI, 'm-', 'LineWidth', 2); hold on;
xlabel('Current I [A]'); ylabel('dCOP/dI');
title('Derivative: COP vs I');
grid on;

subplot(2,2,4);  % 第二列下方：d(T_c)/dCOP
plot(COP_valid, dT_dCOP, 'c-', 'LineWidth', 2); hold on;
% 标出峰值点，画辅助虚线
scatter(COP_peakDT, dT_peakDT, 80, 'r', 'filled', 'Marker','p');
xline(COP_peakDT, 'r--');
yline(dT_peakDT, 'r--');
text(COP_peakDT, dT_peakDT, sprintf(' Peak = %.2f', dT_peakDT), ...
    'FontSize',10,'Color','r','VerticalAlignment','bottom','HorizontalAlignment','left');
xlabel('COP = Q_c / P_{in}'); ylabel('dT_c/dCOP [°C]');
title('Derivative: T_c vs COP');
grid on;

figure(1); hold on;
Tc = T_env; 
Th = T_env;
Tc_hist_peak = zeros(size(time));
Th_hist_peak = zeros(size(time));
Tc_hist_peak(1) = Tc;
Th_hist_peak(1) = Th;
for n = 2:length(time)
    Qc = Qc_fun(I_peakDT, Tc, Th);
    Qc_load = Q_load(Tc);
    dTc_dt = (Qc_load - Qc) / C_cold;
    Qh = Qh_fun(I_peakDT, Tc, Th);
    Qh_sink_val = Qh_sink(Th);
    dTh_dt = (Qh - Qh_sink_val) / C_hot;

    Tc = max(Tc + dTc_dt*dt, 1);
    Th = max(Th + dTh_dt*dt, 1);

    Tc_hist_peak(n) = Tc;
    Th_hist_peak(n) = Th;
end
plot(time, Tc_hist_peak - 273.15, 'r-', 'LineWidth', 2);
legend_entries{end+1} = sprintf('I (Max dT/dCOP)=%.2f A', I_peakDT);
legend(legend_entries,'Location','northeast');

%% =================== 4. T_c vs P, 边际收益分析 ===================
% 假设已有 Pin_ss (W) 和 Tc_ss (K)
P = Pin_ss(:);    
T = Tc_ss(:);

valid = ~isnan(P) & ~isnan(T) & (P>0);
P = P(valid);
T = T(valid) - 273.15;

[P, idx] = sort(P);
T = T(idx);

pp = csaps(P, T, 0.5);
P_fine = linspace(min(P), max(P), 500)';
T_fine = fnval(pp, P_fine);

dT_dP = gradient(T_fine, P_fine); 
marginal = -dT_dP;
d2T_dP2 = gradient(dT_dP, P_fine);

dx = gradient(P_fine);
dy = gradient(T_fine);
d2x = gradient(dx);
d2y = gradient(dy);
curvature = abs(d2x .* dy - dx .* d2y) ./ ( (dx.^2 + dy.^2).^(3/2) + eps );

[~, k_elbow] = max(curvature);
P_elbow = P_fine(k_elbow);
T_elbow = T_fine(k_elbow);
marginal_elbow = marginal(k_elbow);

T_targets = [15, 10, 5, 0];  % °C
colors_lines = [0.7 0 0; 0 0.5 0; 0 0 0.5; 0.5 0 0.5];

figure(4); clf;
subplot(2,1,1);
plot(P_fine, T_fine, 'b-', 'LineWidth',1.5); hold on;
scatter(P, T, 30, 'k', 'filled');
plot(P_elbow, T_elbow, 'rp', 'MarkerSize',12, 'MarkerFaceColor','r');
xlabel('Input power P [W]'); ylabel('Steady T_c [°C]');
title('T_c vs P (spline-smoothed)');
hold on;
for k = 1:numel(T_targets)
    T_val = T_targets(k);
    P_val = interp1(T_fine, P_fine, T_val, 'linear', 'extrap');
    
    xline(P_val, '--', 'Color', colors_lines(k,:), 'LineWidth', 1.2);
    
    I_val = interp1(Pin_ss, I_list_Qc, P_val, 'linear', 'extrap');
    
    y_text = max(T_fine) + 0.5;
    text(P_val + 0.2, y_text, sprintf('%.2f A', I_val), ...
        'Color', colors_lines(k,:), 'FontSize', 9, 'VerticalAlignment','bottom','HorizontalAlignment','left');
end
legend('smoothed','raw','elbow','Location','best');


subplot(2,2,3);
plot(P_fine, marginal, 'm-', 'LineWidth',1.5); hold on;
yline(0,'k--');
plot(P_elbow, marginal_elbow, 'rp', 'MarkerFaceColor','r');
xlabel('P [W]'); ylabel('Marginal benefit b(P) = -dT/dP [°C/W]');
title('Marginal benefit curve');

subplot(2,2,4);
plot(P_fine, curvature, 'g-', 'LineWidth',1.2); hold on;
plot(P_elbow, curvature(k_elbow), 'rp', 'MarkerFaceColor','r');
xlabel('P [W]'); ylabel('Curvature');
title('Curvature (elbow detection)');

fprintf('Elbow (max curvature) at P = %.3f W, T_c = %.3f °C, marginal = %.4f °C/W\n', ...
    P_elbow, T_elbow, marginal_elbow);


figure(2); clf; hold on; grid on;

% 左侧 Y 轴：Qc
yyaxis left
hQc = plot(I_list_Qc, Qc_ss, '-', 'LineWidth', lineWidth, 'Color', [0.3 0.6 1], ...
    'DisplayName','Q_c');  % 浅蓝色实线
ylabel('Cooling power Q_c [W]');
xlabel('Current I [A]');
title('Q_c and Input Power vs I');

% 标出最大 Qc 点
[~, idxQc] = max(Qc_ss);
scatter(I_list_Qc(idxQc), Qc_ss(idxQc), 64, 'k', 'filled', 'Marker','p');
text(I_list_Qc(idxQc), Qc_ss(idxQc), sprintf('  I_{Qmax}=%.2f A', I_list_Qc(idxQc)), ...
    'VerticalAlignment','bottom');

% 右侧 Y 轴：输入功率 Pin
yyaxis right
hPin = plot(I_list_Qc, Pin_ss, '-', 'LineWidth', lineWidth, 'Color', [1 0.3 0.6], ...
    'DisplayName','P_{in}');  % 红色实线
ylabel('Input Power P_{in} [W]');
% ========== 标注峰值 dT/dCOP ==========
yyaxis left
xline(I_peakDT, 'r--', 'LineWidth', 1);
text(I_peakDT, max(Qc_ss)*0.98, sprintf('I=%.2f A', I_peakDT), ...
         'Color','r', 'FontSize', 9, ...
         'VerticalAlignment','bottom','HorizontalAlignment','left');
Qc_peak_val = interp1(I_list_Qc, Qc_ss, I_peakDT);
Pin_peak_val = interp1(I_list_Qc, Pin_ss, I_peakDT);

scatter(I_peakDT, Qc_peak_val, 80, 'b', 'filled', 'Marker','p');
text(I_peakDT, Qc_peak_val, sprintf(' Qc=%.2f W', Qc_peak_val), ...
     'Color','r','VerticalAlignment','bottom','HorizontalAlignment','left');

yyaxis right
scatter(I_peakDT, Pin_peak_val, 80, 'r', 'filled', 'Marker','p');
text(I_peakDT, Pin_peak_val, sprintf(' Pin=%.2f W', Pin_peak_val), ...
     'Color','r','VerticalAlignment','top','HorizontalAlignment','left');

for k = 1:numel(T_targets)
    T_val = T_targets(k);
    P_val = interp1(T_fine, P_fine, T_val, 'linear', 'extrap');
    I_val = interp1(Pin_ss, I_list_Qc, P_val, 'linear', 'extrap');
    Qc_val = interp1(I_list_Qc, Qc_ss, I_val);
    Pin_val = interp1(I_list_Qc, Pin_ss, I_val);
    
    % 虚线
    yyaxis left
    xline(I_val, '--', 'Color', colors_lines(k,:), 'LineWidth', 1.2, ...
          'DisplayName', sprintf('T=%d°C', T_val));
    
    % Qc 和 Pin 点
    yyaxis left
    scatter(I_val, Qc_val, 80, 'filled', 'Marker','p', ...
            'MarkerEdgeColor', colors_lines(k,:), 'MarkerFaceColor', colors_lines(k,:));
    text(I_val, Qc_val, sprintf(' Qc=%.2f W', Qc_val), ...
     'Color','r','VerticalAlignment','bottom','HorizontalAlignment','left');

    yyaxis right
    scatter(I_val, Pin_val, 80, 'filled', 'Marker','p', ...
            'MarkerEdgeColor', colors_lines(k,:), 'MarkerFaceColor', colors_lines(k,:));
    text(I_val, Pin_val, sprintf(' Pin=%.2f W', Pin_val), ...
     'Color','r','VerticalAlignment','top','HorizontalAlignment','left');
    
    yyaxis left
    text(I_val, max(Qc_ss)*0.98, sprintf('I=%.2f A', I_val), ...
         'Color', colors_lines(k,:), 'FontSize', 9, ...
         'VerticalAlignment','bottom','HorizontalAlignment','right');
    text(I_val, max(Qc_ss)*0.89, sprintf('T=%.2f C', T_targets(k)), ...
         'Color', colors_lines(k,:), 'FontSize', 9, ...
         'VerticalAlignment','bottom','HorizontalAlignment','right');
end
legend([hQc, hPin], {'Q_c','P_{in}'}, 'Location','northwest');






