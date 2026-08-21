import React from 'react';
import { Outlet, NavLink, useNavigate } from 'react-router-dom';
import { LayoutGrid, Settings as SettingsIcon, HelpCircle, LogOut } from 'lucide-react';
import './DashboardLayout.css';

export default function DashboardLayout() {
  const navigate = useNavigate();

  const handleLogout = () => {
    navigate('/');
  };

  return (
    <div className="dashboard-container">
      {/* Sidebar */}
      <aside className="sidebar">
        <div className="sidebar-header">
          <span className="logo-text">Menu</span>
        </div>
        
        <nav className="sidebar-nav">
          <NavLink to="/dashboard/subscriptions" className={({isActive}) => isActive ? "nav-item active" : "nav-item"}>
            <LayoutGrid size={20} />
            <span>Подписки</span>
          </NavLink>
          
          <NavLink to="/dashboard/settings" className={({isActive}) => isActive ? "nav-item active" : "nav-item"}>
            <SettingsIcon size={20} />
            <span>Настройки</span>
          </NavLink>
          
          <NavLink to="/dashboard/faq" className={({isActive}) => isActive ? "nav-item active" : "nav-item"}>
            <HelpCircle size={20} />
            <span>Поддержка</span>
          </NavLink>
        </nav>

        <div className="sidebar-footer">
          <button className="logout-btn" onClick={handleLogout}>
            <LogOut size={20} />
            <span>Выйти</span>
          </button>
        </div>
      </aside>

      {/* Main Content */}
      <main className="dashboard-main">
        <Outlet />
      </main>
    </div>
  );
}
